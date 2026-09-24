/* lrg-wall-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Static XZ box collision. Every query is defined by a brute-force loop
 * over the walls in insertion order; the uniform-grid broadphase only
 * narrows that loop to a superset of the walls that could matter and
 * then visits the survivors in ascending index order, so results
 * (including tie-breaking) are bit-for-bit identical.
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-wall-set.h"

/* Slab test: an axis whose displacement is below this is treated as fixed. */
#define WALL_SWEEP_PARALLEL_EPSILON (1e-12)
/* Motion below this L1 length ends lrg_wall_set_move_slide(). */
#define WALL_SLIDE_MIN_MOTION       (1e-10)
/* Distance an agent is pushed back out along the contact normal. */
#define WALL_SLIDE_PUSH_OUT         (1e-7)
/* Contact iterations per lrg_wall_set_move_slide() call. */
#define WALL_SLIDE_ITERATIONS       (4)
/* Broadphase cells per axis; larger worlds get proportionally larger cells. */
#define WALL_GRID_MAX_CELLS_AXIS    (512)
/* Absolute slack added to broadphase query rectangles (world units). */
#define WALL_GRID_MARGIN            (1e-3)
/* Relative slack, scaled by coordinate magnitude, for rounding in sweeps. */
#define WALL_GRID_MARGIN_RELATIVE   (1e-9)
/* Candidate indices kept on the stack before a query spills to the heap. */
#define WALL_QUERY_STACK            (128)

/* ------------------------------------------------------------------------ */
/* LrgWall boxed type                                                       */
/* ------------------------------------------------------------------------ */

G_DEFINE_BOXED_TYPE (LrgWall, lrg_wall, lrg_wall_copy, lrg_wall_free)

LrgWall *
lrg_wall_new (gdouble x,
              gdouble z,
              gdouble half_x,
              gdouble half_z,
              gdouble height,
              guint   kind,
              guint   tag)
{
    LrgWall *wall;

    wall = g_new0 (LrgWall, 1);
    wall->x = x;
    wall->z = z;
    wall->half_x = half_x;
    wall->half_z = half_z;
    wall->height = height;
    wall->kind = kind;
    wall->tag = tag;
    return wall;
}

LrgWall *
lrg_wall_copy (const LrgWall *self)
{
    LrgWall *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgWall, 1);
    *copy = *self;
    return copy;
}

void
lrg_wall_free (LrgWall *self)
{
    g_free (self);
}

/* ------------------------------------------------------------------------ */
/* Wall set                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gdouble x;
    gdouble z;
    gdouble half_x;
    gdouble half_z;
    guint   tag;
} WallVolume;

struct _LrgWallSet
{
    GObject  parent_instance;

    GArray  *walls;        /* LrgWall, insertion order */
    GArray  *volumes;      /* WallVolume, insertion order */
    gdouble  limit;        /* world coordinate limit */
    gdouble  cell_size;    /* configured cell size, 0 = brute force */

    /* Broadphase (valid when grid_built). Cells are stored in CSR form:
     * the walls touching cell c are cell_items[cell_start[c] ..
     * cell_start[c + 1]), each list in ascending wall index order. */
    gboolean grid_built;
    gdouble  grid_min_x;
    gdouble  grid_min_z;
    gdouble  grid_max_x;
    gdouble  grid_max_z;
    gdouble  grid_cell_x;
    gdouble  grid_cell_z;
    guint    grid_nx;
    guint    grid_nz;
    guint   *cell_start;
    guint   *cell_items;
};

G_DEFINE_TYPE (LrgWallSet, lrg_wall_set, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_BOUNDS_LIMIT,
    PROP_CELL_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * WallQuery:
 *
 * Candidate walls for one query, in ascending index order without
 * duplicates. Either borrows a single cell's list (no copy), or owns a
 * merged list in the stack buffer / heap spill.
 */
typedef struct
{
    const guint *items;
    guint        n_items;
    gboolean     brute;
    guint        n_walls;
    guint        stack[WALL_QUERY_STACK];
    guint       *heap;
    guint        capacity;
} WallQuery;

static void
grid_invalidate (LrgWallSet *self)
{
    self->grid_built = FALSE;
    g_clear_pointer (&self->cell_start, g_free);
    g_clear_pointer (&self->cell_items, g_free);
    self->grid_nx = 0;
    self->grid_nz = 0;
}

/* grid_axis_cell:
 * Maps a coordinate (already clamped into the grid bounds) onto a cell
 * index. floor() of a correctly rounded quotient is monotonic, so any two
 * overlapping intervals map to overlapping cell ranges. */
static guint
grid_axis_cell (gdouble value,
                gdouble minimum,
                gdouble cell,
                guint   count)
{
    gdouble index;

    index = floor ((value - minimum) / cell);
    if (!(index > 0.0))
        return 0;
    if (index >= (gdouble)(count - 1))
        return count - 1;
    return (guint)index;
}

/* grid_axis_setup:
 * Chooses the per-axis cell size and count for a span, growing the cell
 * when the configured size would exceed WALL_GRID_MAX_CELLS_AXIS. */
static void
grid_axis_setup (gdouble  span,
                 gdouble  cell_size,
                 gdouble *cell_out,
                 guint   *count_out)
{
    gdouble cell;
    gdouble cells;

    cell = cell_size;
    if (span / cell >= (gdouble)WALL_GRID_MAX_CELLS_AXIS)
        cell = span / (gdouble)(WALL_GRID_MAX_CELLS_AXIS - 1);
    if (!(cell > 0.0) || !isfinite (cell))
        cell = 1.0;

    cells = floor (span / cell) + 1.0;
    if (!(cells >= 1.0))
        cells = 1.0;
    if (cells > (gdouble)WALL_GRID_MAX_CELLS_AXIS)
        cells = (gdouble)WALL_GRID_MAX_CELLS_AXIS;

    *cell_out = cell;
    *count_out = (guint)cells;
}

/* grid_build:
 * Builds the CSR grid with a counting pass and a fill pass. Walls are
 * visited in ascending index order in both passes, so every cell's list
 * stays sorted. */
static void
grid_build (LrgWallSet *self)
{
    guint  n;
    guint  i;
    guint  n_cells;
    guint *cursor;
    guint  pass;

    if (self->grid_built || self->cell_size <= 0.0)
        return;

    n = self->walls->len;
    if (n == 0)
        return;

    /* Bounds of every wall's footprint. */
    self->grid_min_x = G_MAXDOUBLE;
    self->grid_min_z = G_MAXDOUBLE;
    self->grid_max_x = -G_MAXDOUBLE;
    self->grid_max_z = -G_MAXDOUBLE;
    for (i = 0; i < n; i++)
    {
        const LrgWall *w = &g_array_index (self->walls, LrgWall, i);

        self->grid_min_x = MIN (self->grid_min_x, w->x - w->half_x);
        self->grid_min_z = MIN (self->grid_min_z, w->z - w->half_z);
        self->grid_max_x = MAX (self->grid_max_x, w->x + w->half_x);
        self->grid_max_z = MAX (self->grid_max_z, w->z + w->half_z);
    }

    grid_axis_setup (self->grid_max_x - self->grid_min_x, self->cell_size,
                     &self->grid_cell_x, &self->grid_nx);
    grid_axis_setup (self->grid_max_z - self->grid_min_z, self->cell_size,
                     &self->grid_cell_z, &self->grid_nz);

    n_cells = self->grid_nx * self->grid_nz;
    self->cell_start = g_new0 (guint, n_cells + 1);
    cursor = g_new0 (guint, n_cells);

    /* Pass 0 counts entries per cell, pass 1 stores wall indices. */
    for (pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            guint c;

            for (c = 0; c < n_cells; c++)
                self->cell_start[c + 1] += self->cell_start[c];
            self->cell_items = g_new (guint, MAX (1u, self->cell_start[n_cells]));
            for (c = 0; c < n_cells; c++)
                cursor[c] = self->cell_start[c];
        }

        for (i = 0; i < n; i++)
        {
            const LrgWall *w = &g_array_index (self->walls, LrgWall, i);
            guint          x0, x1, z0, z1, cx, cz;

            x0 = grid_axis_cell (w->x - w->half_x, self->grid_min_x,
                                 self->grid_cell_x, self->grid_nx);
            x1 = grid_axis_cell (w->x + w->half_x, self->grid_min_x,
                                 self->grid_cell_x, self->grid_nx);
            z0 = grid_axis_cell (w->z - w->half_z, self->grid_min_z,
                                 self->grid_cell_z, self->grid_nz);
            z1 = grid_axis_cell (w->z + w->half_z, self->grid_min_z,
                                 self->grid_cell_z, self->grid_nz);

            for (cz = z0; cz <= z1; cz++)
                for (cx = x0; cx <= x1; cx++)
                {
                    guint cell = cz * self->grid_nx + cx;

                    if (pass == 0)
                        self->cell_start[cell + 1]++;
                    else
                        self->cell_items[cursor[cell]++] = i;
                }
        }
    }

    g_free (cursor);
    self->grid_built = TRUE;
}

static int
compare_guint (const void *a,
               const void *b)
{
    guint left = *(const guint *)a;
    guint right = *(const guint *)b;

    return (left > right) - (left < right);
}

static void
query_push (WallQuery *query,
            guint      value)
{
    guint *buffer;

    if (query->n_items == query->capacity)
    {
        guint new_capacity = query->capacity * 2;

        if (query->heap == NULL)
        {
            query->heap = g_new (guint, new_capacity);
            memcpy (query->heap, query->stack, sizeof (guint) * query->n_items);
        }
        else
        {
            query->heap = g_renew (guint, query->heap, new_capacity);
        }
        query->capacity = new_capacity;
    }

    buffer = query->heap != NULL ? query->heap : query->stack;
    buffer[query->n_items++] = value;
}

/* query_rect:
 * Collects the candidate walls for the rectangle [min_x, max_x] x
 * [min_z, max_z] (already including the agent radius). The rectangle is
 * widened by an absolute and a relative margin so floating-point rounding
 * in the exact tests can never make a wall outside the candidates hit. */
static void
query_rect (LrgWallSet *self,
            WallQuery  *query,
            gdouble     min_x,
            gdouble     min_z,
            gdouble     max_x,
            gdouble     max_z)
{
    gdouble margin;
    guint   x0, x1, z0, z1, cx, cz;
    guint  *buffer;
    guint   i, out;

    query->items = NULL;
    query->n_items = 0;
    query->brute = FALSE;
    query->heap = NULL;
    query->capacity = WALL_QUERY_STACK;
    query->n_walls = self->walls->len;

    if (self->walls->len == 0)
        return;

    if (self->cell_size <= 0.0)
    {
        query->brute = TRUE;
        return;
    }

    grid_build (self);

    margin = WALL_GRID_MARGIN + WALL_GRID_MARGIN_RELATIVE *
        MAX (MAX (fabs (min_x), fabs (max_x)), MAX (fabs (min_z), fabs (max_z)));
    min_x -= margin;
    min_z -= margin;
    max_x += margin;
    max_z += margin;

    /* Written as negated comparisons so a NaN rectangle falls back to the
     * brute-force loop instead of silently returning nothing. */
    if (!(min_x <= max_x) || !(min_z <= max_z))
    {
        query->brute = TRUE;
        return;
    }
    if (max_x < self->grid_min_x || min_x > self->grid_max_x ||
        max_z < self->grid_min_z || min_z > self->grid_max_z)
        return;

    x0 = grid_axis_cell (MAX (min_x, self->grid_min_x), self->grid_min_x,
                         self->grid_cell_x, self->grid_nx);
    x1 = grid_axis_cell (MIN (max_x, self->grid_max_x), self->grid_min_x,
                         self->grid_cell_x, self->grid_nx);
    z0 = grid_axis_cell (MAX (min_z, self->grid_min_z), self->grid_min_z,
                         self->grid_cell_z, self->grid_nz);
    z1 = grid_axis_cell (MIN (max_z, self->grid_max_z), self->grid_min_z,
                         self->grid_cell_z, self->grid_nz);

    /* One cell: its list is already sorted and unique. */
    if (x0 == x1 && z0 == z1)
    {
        guint cell = z0 * self->grid_nx + x0;

        query->items = self->cell_items + self->cell_start[cell];
        query->n_items = self->cell_start[cell + 1] - self->cell_start[cell];
        return;
    }

    /* Several cells: gather, sort, drop duplicates. */
    for (cz = z0; cz <= z1; cz++)
        for (cx = x0; cx <= x1; cx++)
        {
            guint cell = cz * self->grid_nx + cx;
            guint k;

            for (k = self->cell_start[cell]; k < self->cell_start[cell + 1]; k++)
                query_push (query, self->cell_items[k]);
        }

    buffer = query->heap != NULL ? query->heap : query->stack;
    qsort (buffer, query->n_items, sizeof (guint), compare_guint);
    for (i = 0, out = 0; i < query->n_items; i++)
        if (out == 0 || buffer[out - 1] != buffer[i])
            buffer[out++] = buffer[i];
    query->n_items = out;
    query->items = buffer;
}

static void
query_clear (WallQuery *query)
{
    g_clear_pointer (&query->heap, g_free);
}

/* query_count / query_at:
 * Iterate the candidates; brute-force queries enumerate every wall. */
static guint
query_count (const WallQuery *query)
{
    return query->brute ? query->n_walls : query->n_items;
}

static guint
query_at (const WallQuery *query,
          guint            position)
{
    return query->brute ? position : query->items[position];
}

static gboolean
valid_position (LrgWallSet *self,
                gdouble     x,
                gdouble     z)
{
    return isfinite (x) && isfinite (z) &&
           fabs (x) <= self->limit && fabs (z) <= self->limit;
}

/* sweep:
 * Slab intersection of the segment p + t*d, t in [0, 1], with the wall
 * expanded by radius. Also supplies the entering face normal. This is the
 * reference algorithm; do not reorder its arithmetic. */
static gboolean
sweep (gdouble        x,
       gdouble        z,
       gdouble        dx,
       gdouble        dz,
       const LrgWall *b,
       gdouble        radius,
       gdouble       *time,
       gdouble       *nx,
       gdouble       *nz)
{
    gdouble p[2];
    gdouble d[2];
    gdouble low[2];
    gdouble high[2];
    gdouble enter = -G_MAXDOUBLE;
    gdouble leave = G_MAXDOUBLE;
    gdouble normal[2] = { 0, 0 };
    guint   axis;

    p[0] = x;
    p[1] = z;
    d[0] = dx;
    d[1] = dz;
    low[0] = b->x - b->half_x - radius;
    low[1] = b->z - b->half_z - radius;
    high[0] = b->x + b->half_x + radius;
    high[1] = b->z + b->half_z + radius;

    for (axis = 0; axis < 2; axis++)
    {
        gdouble a, c, n;

        if (fabs (d[axis]) < WALL_SWEEP_PARALLEL_EPSILON)
        {
            if (p[axis] <= low[axis] || p[axis] >= high[axis])
                return FALSE;
            continue;
        }
        a = (low[axis] - p[axis]) / d[axis];
        c = (high[axis] - p[axis]) / d[axis];
        n = -1;
        if (a > c)
        {
            gdouble temp = a;

            a = c;
            c = temp;
            n = 1;
        }
        if (a > enter)
        {
            enter = a;
            normal[0] = normal[1] = 0;
            normal[axis] = n;
        }
        leave = MIN (leave, c);
        if (enter > leave)
            return FALSE;
    }
    if (enter < 0 || enter > 1 || leave < 0)
        return FALSE;
    *time = enter;
    *nx = normal[0];
    *nz = normal[1];
    return TRUE;
}

/* query_segment:
 * Candidates for a segment from (x, z) moving (dx, dz) with an agent of
 * the given radius: the segment's bounding box grown by the radius. */
static void
query_segment (LrgWallSet *self,
               WallQuery  *query,
               gdouble     x,
               gdouble     z,
               gdouble     dx,
               gdouble     dz,
               gdouble     radius)
{
    gdouble ex = x + dx;
    gdouble ez = z + dz;

    query_rect (self, query,
                MIN (x, ex) - radius, MIN (z, ez) - radius,
                MAX (x, ex) + radius, MAX (z, ez) + radius);
}

static void
lrg_wall_set_finalize (GObject *object)
{
    LrgWallSet *self = LRG_WALL_SET (object);

    grid_invalidate (self);
    g_clear_pointer (&self->walls, g_array_unref);
    g_clear_pointer (&self->volumes, g_array_unref);

    G_OBJECT_CLASS (lrg_wall_set_parent_class)->finalize (object);
}

static void
lrg_wall_set_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgWallSet *self = LRG_WALL_SET (object);

    switch (prop_id)
    {
    case PROP_BOUNDS_LIMIT:
        g_value_set_double (value, self->limit);
        break;
    case PROP_CELL_SIZE:
        g_value_set_double (value, self->cell_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_wall_set_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgWallSet *self = LRG_WALL_SET (object);

    switch (prop_id)
    {
    case PROP_BOUNDS_LIMIT:
        lrg_wall_set_set_bounds_limit (self, g_value_get_double (value));
        break;
    case PROP_CELL_SIZE:
        lrg_wall_set_set_cell_size (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_wall_set_class_init (LrgWallSetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_wall_set_finalize;
    object_class->get_property = lrg_wall_set_get_property;
    object_class->set_property = lrg_wall_set_set_property;

    /**
     * LrgWallSet:bounds-limit:
     *
     * World coordinate limit; see lrg_wall_set_set_bounds_limit().
     */
    properties[PROP_BOUNDS_LIMIT] =
        g_param_spec_double ("bounds-limit", "Bounds limit",
                             "Largest valid |x| or |z|",
                             G_MINDOUBLE, G_MAXDOUBLE, G_MAXDOUBLE,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgWallSet:cell-size:
     *
     * Broadphase cell size; 0 selects brute force.
     */
    properties[PROP_CELL_SIZE] =
        g_param_spec_double ("cell-size", "Cell size",
                             "Broadphase grid cell edge length",
                             0.0, G_MAXDOUBLE, LRG_WALL_SET_DEFAULT_CELL_SIZE,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_wall_set_init (LrgWallSet *self)
{
    self->walls = g_array_new (FALSE, FALSE, sizeof (LrgWall));
    self->volumes = g_array_new (FALSE, FALSE, sizeof (WallVolume));
    self->limit = G_MAXDOUBLE;
    self->cell_size = LRG_WALL_SET_DEFAULT_CELL_SIZE;
}

LrgWallSet *
lrg_wall_set_new (void)
{
    return g_object_new (LRG_TYPE_WALL_SET, NULL);
}

guint
lrg_wall_set_add (LrgWallSet    *self,
                  const LrgWall *wall)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), LRG_WALL_SET_INVALID_INDEX);
    g_return_val_if_fail (wall != NULL, LRG_WALL_SET_INVALID_INDEX);

    /* Reject anything the exact tests cannot handle: non-finite values,
     * negative extents, and edges that overflow to infinity. */
    if (!isfinite (wall->x) || !isfinite (wall->z) ||
        !isfinite (wall->half_x) || !isfinite (wall->half_z) ||
        !isfinite (wall->height) ||
        wall->half_x < 0.0 || wall->half_z < 0.0 ||
        !isfinite (wall->x - wall->half_x) || !isfinite (wall->x + wall->half_x) ||
        !isfinite (wall->z - wall->half_z) || !isfinite (wall->z + wall->half_z) ||
        self->walls->len >= G_MAXUINT - 1)
        return LRG_WALL_SET_INVALID_INDEX;

    g_array_append_val (self->walls, *wall);
    grid_invalidate (self);
    return self->walls->len - 1;
}

guint
lrg_wall_set_add_box (LrgWallSet *self,
                      gdouble     x,
                      gdouble     z,
                      gdouble     half_x,
                      gdouble     half_z,
                      gdouble     height,
                      guint       kind,
                      guint       tag)
{
    LrgWall wall;

    wall.x = x;
    wall.z = z;
    wall.half_x = half_x;
    wall.half_z = half_z;
    wall.height = height;
    wall.kind = kind;
    wall.tag = tag;
    return lrg_wall_set_add (self, &wall);
}

guint
lrg_wall_set_get_count (LrgWallSet *self)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), 0);

    return self->walls->len;
}

gboolean
lrg_wall_set_get (LrgWallSet *self,
                  guint       index,
                  LrgWall    *out)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), FALSE);
    g_return_val_if_fail (out != NULL, FALSE);

    if (index >= self->walls->len)
        return FALSE;
    *out = g_array_index (self->walls, LrgWall, index);
    return TRUE;
}

void
lrg_wall_set_clear (LrgWallSet *self)
{
    g_return_if_fail (LRG_IS_WALL_SET (self));

    g_array_set_size (self->walls, 0);
    g_array_set_size (self->volumes, 0);
    grid_invalidate (self);
}

void
lrg_wall_set_set_bounds_limit (LrgWallSet *self,
                               gdouble     limit)
{
    g_return_if_fail (LRG_IS_WALL_SET (self));

    if (!(limit > 0.0) || self->limit == limit)
        return;
    self->limit = limit;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_LIMIT]);
}

gdouble
lrg_wall_set_get_bounds_limit (LrgWallSet *self)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), G_MAXDOUBLE);

    return self->limit;
}

void
lrg_wall_set_set_cell_size (LrgWallSet *self,
                            gdouble     cell_size)
{
    g_return_if_fail (LRG_IS_WALL_SET (self));

    if (!isfinite (cell_size) || cell_size < 0.0 || self->cell_size == cell_size)
        return;
    self->cell_size = cell_size;
    grid_invalidate (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CELL_SIZE]);
}

gdouble
lrg_wall_set_get_cell_size (LrgWallSet *self)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), 0.0);

    return self->cell_size;
}

void
lrg_wall_set_build_index (LrgWallSet *self)
{
    g_return_if_fail (LRG_IS_WALL_SET (self));

    grid_build (self);
}

gboolean
lrg_wall_set_is_clear (LrgWallSet *self,
                       gdouble     x,
                       gdouble     z,
                       gdouble     radius)
{
    WallQuery query;
    gboolean  clear = TRUE;
    guint     i;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), FALSE);

    if (!valid_position (self, x, z) || !isfinite (radius) ||
        radius < 0 || radius > LRG_WALL_SET_MAX_RADIUS)
        return FALSE;

    query_rect (self, &query, x - radius, z - radius, x + radius, z + radius);
    for (i = 0; i < query_count (&query); i++)
    {
        const LrgWall *b = &g_array_index (self->walls, LrgWall, query_at (&query, i));

        if (fabs (x - b->x) < b->half_x + radius && fabs (z - b->z) < b->half_z + radius)
        {
            clear = FALSE;
            break;
        }
    }
    query_clear (&query);
    return clear;
}

gboolean
lrg_wall_set_move_slide (LrgWallSet *self,
                         gdouble     radius,
                         gdouble    *x,
                         gdouble    *z,
                         gdouble     dx,
                         gdouble     dz)
{
    guint   iteration;
    gdouble px, pz;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), FALSE);

    if (x == NULL || z == NULL || !lrg_wall_set_is_clear (self, *x, *z, radius) ||
        !isfinite (dx) || !isfinite (dz) ||
        fabs (dx) > self->limit * 2 || fabs (dz) > self->limit * 2)
        return FALSE;

    px = *x;
    pz = *z;
    dx = CLAMP (px + dx, -self->limit, self->limit) - px;
    dz = CLAMP (pz + dz, -self->limit, self->limit) - pz;

    for (iteration = 0;
         iteration < WALL_SLIDE_ITERATIONS && (fabs (dx) + fabs (dz) > WALL_SLIDE_MIN_MOTION);
         iteration++)
    {
        gdouble   first = 1;
        gdouble   nx = 0;
        gdouble   nz = 0;
        gboolean  blocked = FALSE;
        WallQuery query;
        guint     i;

        /* Earliest contact; on equal times the later wall wins, exactly
         * like the reference loop (candidates are in ascending order). */
        query_segment (self, &query, px, pz, dx, dz, radius);
        for (i = 0; i < query_count (&query); i++)
        {
            const LrgWall *b = &g_array_index (self->walls, LrgWall, query_at (&query, i));
            gdouble        time, bx, bz;

            if (sweep (px, pz, dx, dz, b, radius, &time, &bx, &bz) && time <= first)
            {
                first = time;
                nx = bx;
                nz = bz;
                blocked = TRUE;
            }
        }
        query_clear (&query);

        px += dx * first;
        pz += dz * first;
        if (!blocked)
            break;

        /* Step back out of the contact face and slide along it. */
        px += nx * WALL_SLIDE_PUSH_OUT;
        pz += nz * WALL_SLIDE_PUSH_OUT;
        dx *= 1 - first;
        dz *= 1 - first;
        if (nx != 0)
            dx = 0;
        if (nz != 0)
            dz = 0;
    }

    *x = px;
    *z = pz;
    return TRUE;
}

gboolean
lrg_wall_set_segment_clear (LrgWallSet *self,
                            gdouble     ax,
                            gdouble     az,
                            gdouble     bx,
                            gdouble     bz)
{
    WallQuery query;
    gboolean  clear = TRUE;
    guint     i;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), FALSE);

    if (!lrg_wall_set_is_clear (self, ax, az, 0) || !lrg_wall_set_is_clear (self, bx, bz, 0))
        return FALSE;

    query_segment (self, &query, ax, az, bx - ax, bz - az, 0);
    for (i = 0; i < query_count (&query); i++)
    {
        const LrgWall *b = &g_array_index (self->walls, LrgWall, query_at (&query, i));
        gdouble        time, nx, nz;

        if (sweep (ax, az, bx - ax, bz - az, b, 0, &time, &nx, &nz))
        {
            clear = FALSE;
            break;
        }
    }
    query_clear (&query);
    return clear;
}

gdouble
lrg_wall_set_raycast_distance (LrgWallSet *self,
                               gdouble     ox,
                               gdouble     oz,
                               gdouble     dx,
                               gdouble     dz,
                               gdouble     max_distance,
                               guint      *hit_kind)
{
    WallQuery query;
    gdouble   length;
    gdouble   sx, sz;
    gdouble   best;
    guint     kind = LRG_WALL_SET_NO_HIT;
    guint     i;

    if (hit_kind != NULL)
        *hit_kind = LRG_WALL_SET_NO_HIT;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), 0.0);

    length = hypot (dx, dz);
    if (!isfinite (ox) || !isfinite (oz) || !isfinite (length) || !(length > 0.0) ||
        !isfinite (max_distance) || !(max_distance > 0.0))
        return 0.0;

    /* Full sweep vector: the normalised direction scaled to the range. */
    sx = dx / length * max_distance;
    sz = dz / length * max_distance;
    if (!isfinite (sx) || !isfinite (sz))
        return 0.0;

    best = max_distance;
    query_segment (self, &query, ox, oz, sx, sz, 0);
    for (i = 0; i < query_count (&query); i++)
    {
        const LrgWall *b = &g_array_index (self->walls, LrgWall, query_at (&query, i));
        gdouble        time, nx, nz, distance;

        /* Origin inside a wall: the camera must collapse onto the origin. */
        if (fabs (ox - b->x) < b->half_x && fabs (oz - b->z) < b->half_z)
        {
            best = 0.0;
            kind = b->kind;
            break;
        }
        if (!sweep (ox, oz, sx, sz, b, 0, &time, &nx, &nz))
            continue;
        distance = time * max_distance;
        if (distance < best || (kind == LRG_WALL_SET_NO_HIT && distance <= best))
        {
            best = distance;
            kind = b->kind;
        }
    }
    query_clear (&query);

    if (hit_kind != NULL)
        *hit_kind = kind;
    return best;
}

guint
lrg_wall_set_add_volume (LrgWallSet *self,
                         gdouble     x,
                         gdouble     z,
                         gdouble     half_x,
                         gdouble     half_z,
                         guint       tag)
{
    WallVolume volume;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), LRG_WALL_SET_INVALID_INDEX);

    if (!isfinite (x) || !isfinite (z) || !isfinite (half_x) || !isfinite (half_z) ||
        half_x < 0.0 || half_z < 0.0 || tag > (guint)G_MAXINT ||
        self->volumes->len >= G_MAXUINT - 1)
        return LRG_WALL_SET_INVALID_INDEX;

    volume.x = x;
    volume.z = z;
    volume.half_x = half_x;
    volume.half_z = half_z;
    volume.tag = tag;
    g_array_append_val (self->volumes, volume);
    return self->volumes->len - 1;
}

guint
lrg_wall_set_get_volume_count (LrgWallSet *self)
{
    g_return_val_if_fail (LRG_IS_WALL_SET (self), 0);

    return self->volumes->len;
}

gint
lrg_wall_set_volume_at (LrgWallSet *self,
                        gdouble     x,
                        gdouble     z)
{
    guint i;

    g_return_val_if_fail (LRG_IS_WALL_SET (self), -1);

    if (!isfinite (x) || !isfinite (z))
        return -1;

    for (i = 0; i < self->volumes->len; i++)
    {
        const WallVolume *v = &g_array_index (self->volumes, WallVolume, i);

        if (fabs (x - v->x) <= v->half_x && fabs (z - v->z) <= v->half_z)
            return (gint)v->tag;
    }
    return -1;
}
