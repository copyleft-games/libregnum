/* lrg-nav-poly-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A voxel-baked polygon navigation mesh.  The bake, step by step:
 *
 * 1. Rasterise: every staged triangle is clipped against the columns of
 *    the cell grid (Sutherland-Hodgman against axis-aligned lines) and
 *    each clipped piece adds a solid span [ymin, ymax] to its column.
 *    Overlapping spans merge; the merged span is walkable when the span
 *    with the higher top was (both when the tops lie within max-climb).
 * 2. Filter: a non-walkable span whose top lies within max-climb above a
 *    walkable span below it becomes walkable (kerbs, the risers of steps).
 * 3. Open spans: the space above each walkable solid span up to the next
 *    solid span; kept when at least agent-height tall.  Neighbours (four
 *    directions) connect when their floors differ by at most max-climb
 *    and their open spaces overlap by agent-height.
 * 4. Erode: a two-pass chamfer distance (2 orthogonal, 3 diagonal, in
 *    half cells) from the spans that miss a neighbour; spans closer than
 *    the agent's radius are dropped.  Area boxes tag the rest.
 * 5. Polygons: unassigned spans grow greedily into rectangles (first
 *    along +X, then whole rows along +Z) whose spans are all connected
 *    to their neighbours inside the rectangle and share one tag: convex
 *    polygons by construction, one span per column.
 * 6. Portals: runs of connected span pairs across a rectangle side with
 *    the same neighbouring rectangle become one portal segment.
 * 7. Links: drops found at ledges (max-drop) and the staged off-mesh
 *    links connect polygons; regions are the connected components.
 *
 * Queries keep the cells (floor height and polygon per walkable span,
 * indexed by column) for heights and nearest-point searches.
 */

#include "lrg-nav-poly-mesh.h"
#include "lrg-nav-poly-mesh-cache.h"
#include <gio/gio.h>
#include <math.h>
#include <string.h>

#define MAX_SIDE      (32)          /* longest rectangle side in cells */
#define CLIP_VERTS    (16)
#define FORMAT_MAGIC  "LRGNPM01"
#define FORMAT_VERSION (2u)       /* 2: real floor heights per cell */
#define LINK_DROP_BIT (0x80000000u) /* link reference: a drop link */

enum
{
    PROP_0,
    PROP_CELL_SIZE,
    PROP_CELL_HEIGHT,
    PROP_AGENT_RADIUS,
    PROP_AGENT_HEIGHT,
    PROP_MAX_CLIMB,
    PROP_MAX_SLOPE,
    PROP_MAX_DROP,
    PROP_CORNER_MARGIN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* directions: 0 -X, 1 +Z, 2 +X, 3 -Z */
static const gint dir_x[4] = { -1, 0, 1, 0 };
static const gint dir_z[4] = { 0, 1, 0, -1 };

/* a staged area box */
typedef struct
{
    gfloat  min[3];
    gfloat  max[3];
    guint32 tag;
} AreaBox;

/* an off-mesh link (added or a baked drop) */
typedef struct
{
    gfloat   start[3];
    gfloat   end[3];
    gfloat   cost;
    guint32  tag;
    guint32  poly_a;
    guint32  poly_b;
    gboolean bidirectional;
    gboolean enabled;
    gboolean connected;
} Link;

/* a walkable cell kept after the bake */
typedef struct
{
    gfloat y;       /* the floor's real height */
    gint32 poly;
} Cell;

/* a convex (rectangular) polygon in cells */
typedef struct
{
    gint32   x0;
    gint32   z0;
    gint32   w;
    gint32   h;
    guint32  tag;
    guint32  region;
    gfloat   ymin;
    gfloat   ymax;
    guint32  first_edge;
    guint32  n_edges;
    gboolean enabled;
} Poly;

/* a portal: from its polygon to @to across side @side, @a .. @b */
typedef struct
{
    gint32   to;
    guint32  side;
    gboolean enabled;
    gfloat   a[3];
    gfloat   b[3];
} Edge;

/* a link end attached to a polygon: the link reference, the direction */
typedef struct
{
    guint32  ref;
    gboolean forward;
} LinkUse;

struct _LrgNavPolyMesh
{
    GObject parent_instance;

    /* properties */
    gfloat cell_size;
    gfloat cell_height;
    gfloat agent_radius;
    gfloat agent_height;
    gfloat max_climb;
    gfloat max_slope;
    gfloat max_drop;
    gfloat corner_margin;

    /* staged input */
    GArray *triangles;      /* gfloat, nine per triangle */
    GArray *walkable;       /* guint8 per triangle */
    GArray *boxes;          /* AreaBox */
    GArray *links;          /* Link, the added ones (ids) */

    /* the bake */
    gboolean baked;
    gfloat   origin[3];
    gfloat   bake_cs;
    gfloat   bake_ch;
    gint32   width;
    gint32   depth;
    guint32 *col_first;     /* width * depth + 1 */
    GArray  *cells;         /* Cell */
    GArray  *polys;         /* Poly */
    GArray  *edges;         /* Edge */
    GArray  *drops;         /* Link, generated */
    GHashTable *link_uses;  /* poly -> GArray of LinkUse */
    guint    n_regions;
    gdouble  bake_seconds;

    GHashTable *disabled_tags;

    /* A* scratch */
    guint32 *stamp;
    gfloat  *cost;
    gint32  *parent;
    guint32 *parent_via;
    gfloat  *node_pos;
    guint8  *closed;
    guint32  generation;
    guint    scratch_size;
};

G_DEFINE_FINAL_TYPE (LrgNavPolyMesh, lrg_nav_poly_mesh, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/* Object                                                               */

static void
free_bake (LrgNavPolyMesh *self)
{
    g_clear_pointer (&self->col_first, g_free);
    g_clear_pointer (&self->cells, g_array_unref);
    g_clear_pointer (&self->polys, g_array_unref);
    g_clear_pointer (&self->edges, g_array_unref);
    g_clear_pointer (&self->drops, g_array_unref);
    g_clear_pointer (&self->link_uses, g_hash_table_unref);
    g_clear_pointer (&self->stamp, g_free);
    g_clear_pointer (&self->cost, g_free);
    g_clear_pointer (&self->parent, g_free);
    g_clear_pointer (&self->parent_via, g_free);
    g_clear_pointer (&self->node_pos, g_free);
    g_clear_pointer (&self->closed, g_free);
    self->scratch_size = 0;
    self->baked = FALSE;
}

static void
lrg_nav_poly_mesh_finalize (GObject *object)
{
    LrgNavPolyMesh *self = LRG_NAV_POLY_MESH (object);

    free_bake (self);
    g_array_unref (self->triangles);
    g_array_unref (self->walkable);
    g_array_unref (self->boxes);
    g_array_unref (self->links);
    g_hash_table_unref (self->disabled_tags);
    G_OBJECT_CLASS (lrg_nav_poly_mesh_parent_class)->finalize (object);
}

static void
lrg_nav_poly_mesh_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgNavPolyMesh *self = LRG_NAV_POLY_MESH (object);

    switch (prop_id)
    {
    case PROP_CELL_SIZE:    g_value_set_float (value, self->cell_size); break;
    case PROP_CELL_HEIGHT:  g_value_set_float (value, self->cell_height); break;
    case PROP_AGENT_RADIUS: g_value_set_float (value, self->agent_radius); break;
    case PROP_AGENT_HEIGHT: g_value_set_float (value, self->agent_height); break;
    case PROP_MAX_CLIMB:    g_value_set_float (value, self->max_climb); break;
    case PROP_MAX_SLOPE:    g_value_set_float (value, self->max_slope); break;
    case PROP_MAX_DROP:     g_value_set_float (value, self->max_drop); break;
    case PROP_CORNER_MARGIN: g_value_set_float (value, self->corner_margin); break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nav_poly_mesh_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgNavPolyMesh *self = LRG_NAV_POLY_MESH (object);

    switch (prop_id)
    {
    case PROP_CELL_SIZE:    self->cell_size = g_value_get_float (value); break;
    case PROP_CELL_HEIGHT:  self->cell_height = g_value_get_float (value); break;
    case PROP_AGENT_RADIUS: self->agent_radius = g_value_get_float (value); break;
    case PROP_AGENT_HEIGHT: self->agent_height = g_value_get_float (value); break;
    case PROP_MAX_CLIMB:    self->max_climb = g_value_get_float (value); break;
    case PROP_MAX_SLOPE:    self->max_slope = g_value_get_float (value); break;
    case PROP_MAX_DROP:     self->max_drop = g_value_get_float (value); break;
    case PROP_CORNER_MARGIN: self->corner_margin = g_value_get_float (value); break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nav_poly_mesh_class_init (LrgNavPolyMeshClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

    object_class->finalize = lrg_nav_poly_mesh_finalize;
    object_class->get_property = lrg_nav_poly_mesh_get_property;
    object_class->set_property = lrg_nav_poly_mesh_set_property;

    /**
     * LrgNavPolyMesh:cell-size:
     *
     * Edge of a heightfield column in world units.
     */
    properties[PROP_CELL_SIZE] = g_param_spec_float ("cell-size", NULL, NULL,
                                                     0.01f, 100.0f, 0.25f, flags);
    /**
     * LrgNavPolyMesh:cell-height:
     *
     * Height of a heightfield layer in world units.
     */
    properties[PROP_CELL_HEIGHT] = g_param_spec_float ("cell-height", NULL, NULL,
                                                       0.01f, 100.0f, 0.1f, flags);
    /**
     * LrgNavPolyMesh:agent-radius:
     *
     * Radius the surface is eroded by (rounded to whole cells).
     */
    properties[PROP_AGENT_RADIUS] = g_param_spec_float ("agent-radius", NULL, NULL,
                                                        0.0f, 100.0f, 0.3f, flags);
    /**
     * LrgNavPolyMesh:agent-height:
     *
     * Free height the agent needs above the floor.
     */
    properties[PROP_AGENT_HEIGHT] = g_param_spec_float ("agent-height", NULL, NULL,
                                                        0.01f, 1000.0f, 1.8f, flags);
    /**
     * LrgNavPolyMesh:max-climb:
     *
     * Largest step between neighbouring floors.
     */
    properties[PROP_MAX_CLIMB] = g_param_spec_float ("max-climb", NULL, NULL,
                                                     0.0f, 100.0f, 0.45f, flags);
    /**
     * LrgNavPolyMesh:max-slope:
     *
     * Steepest walkable triangle in degrees.
     */
    properties[PROP_MAX_SLOPE] = g_param_spec_float ("max-slope", NULL, NULL,
                                                     0.0f, 89.9f, 50.0f, flags);
    /**
     * LrgNavPolyMesh:max-drop:
     *
     * Highest ledge the bake makes one-way drop links down from; 0 for
     * none.
     */
    properties[PROP_MAX_DROP] = g_param_spec_float ("max-drop", NULL, NULL,
                                                    0.0f, 1000.0f, 0.0f, flags);
    /**
     * LrgNavPolyMesh:corner-margin:
     *
     * How far paths keep from the corners they turn round: each portal
     * is narrowed by this at both ends (never past its middle) before
     * the funnel straightens the corridor.  Read at query time.
     */
    properties[PROP_CORNER_MARGIN] = g_param_spec_float ("corner-margin", NULL, NULL,
                                                         0.0f, 100.0f, 0.0f, flags);
    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_nav_poly_mesh_init (LrgNavPolyMesh *self)
{
    self->cell_size = 0.25f;
    self->cell_height = 0.1f;
    self->agent_radius = 0.3f;
    self->agent_height = 1.8f;
    self->max_climb = 0.45f;
    self->max_slope = 50.0f;
    self->max_drop = 0.0f;
    self->triangles = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->walkable = g_array_new (FALSE, FALSE, sizeof (guint8));
    self->boxes = g_array_new (FALSE, FALSE, sizeof (AreaBox));
    self->links = g_array_new (FALSE, FALSE, sizeof (Link));
    self->disabled_tags = g_hash_table_new (g_direct_hash, g_direct_equal);
}

LrgNavPolyMesh *
lrg_nav_poly_mesh_new (void)
{
    return g_object_new (LRG_TYPE_NAV_POLY_MESH, NULL);
}

/* ------------------------------------------------------------------ */
/* Input                                                                */

void
lrg_nav_poly_mesh_clear_input (LrgNavPolyMesh *self)
{
    g_return_if_fail (LRG_IS_NAV_POLY_MESH (self));

    g_array_set_size (self->triangles, 0);
    g_array_set_size (self->walkable, 0);
    g_array_set_size (self->boxes, 0);
}

static gboolean
finite3 (const gfloat *v)
{
    return isfinite (v[0]) && isfinite (v[1]) && isfinite (v[2]);
}

guint
lrg_nav_poly_mesh_add_triangles (LrgNavPolyMesh *self,
                                 const gfloat   *vertices,
                                 guint           n_floats,
                                 gboolean        walkable)
{
    guint added;
    guint i;
    guint8 flag;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    g_return_val_if_fail (vertices != NULL || n_floats == 0, 0);

    added = 0;
    flag = walkable ? 1 : 0;
    for (i = 0; i + 9 <= n_floats; i += 9)
    {
        if (!finite3 (vertices + i) || !finite3 (vertices + i + 3) || !finite3 (vertices + i + 6))
            continue;
        g_array_append_vals (self->triangles, vertices + i, 9);
        g_array_append_val (self->walkable, flag);
        added++;
    }
    return added;
}

gboolean
lrg_nav_poly_mesh_add_area_box (LrgNavPolyMesh *self,
                                const gfloat   *min,
                                const gfloat   *max,
                                guint32         tag)
{
    AreaBox box;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (min != NULL && max != NULL, FALSE);

    if (tag == 0 || !finite3 (min) || !finite3 (max) ||
        max[0] < min[0] || max[1] < min[1] || max[2] < min[2])
        return FALSE;
    memcpy (box.min, min, sizeof (box.min));
    memcpy (box.max, max, sizeof (box.max));
    box.tag = tag;
    g_array_append_val (self->boxes, box);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Cells and heights                                                    */

static gboolean
column_of (LrgNavPolyMesh *self,
           gfloat          x,
           gfloat          z,
           gint32         *cx,
           gint32         *cz)
{
    gfloat fx;
    gfloat fz;

    fx = floorf ((x - self->origin[0]) / self->bake_cs);
    fz = floorf ((z - self->origin[2]) / self->bake_cs);
    if (!isfinite (fx) || !isfinite (fz))
        return FALSE;
    *cx = (gint32) CLAMP (fx, -1.0f, (gfloat) self->width);
    *cz = (gint32) CLAMP (fz, -1.0f, (gfloat) self->depth);
    return TRUE;
}

static gfloat
cell_y (LrgNavPolyMesh *self,
        gfloat          y)
{
    (void) self;
    return y;
}

/* the floor height of @poly at its cell (@cx, @cz), clamped into it */
static gfloat
poly_height (LrgNavPolyMesh *self,
             guint           poly,
             gint32          cx,
             gint32          cz)
{
    const Poly *p;
    guint32 column;
    guint32 i;

    p = &g_array_index (self->polys, Poly, poly);
    cx = CLAMP (cx, p->x0, p->x0 + p->w - 1);
    cz = CLAMP (cz, p->z0, p->z0 + p->h - 1);
    column = (guint32) cz * (guint32) self->width + (guint32) cx;
    for (i = self->col_first[column]; i < self->col_first[column + 1]; i++)
    {
        const Cell *c = &g_array_index (self->cells, Cell, i);

        if (c->poly == (gint32) poly)
            return cell_y (self, c->y);
    }
    return p->ymin;
}

static gfloat
poly_height_at (LrgNavPolyMesh *self,
                guint           poly,
                gfloat          x,
                gfloat          z)
{
    gint32 cx;
    gint32 cz;

    if (!column_of (self, x, z, &cx, &cz))
        return g_array_index (self->polys, Poly, poly).ymin;
    return poly_height (self, poly, cx, cz);
}

/* the polygon's rectangle in world X/Z */
static void
poly_rect (LrgNavPolyMesh *self,
           const Poly     *p,
           gfloat         *x0,
           gfloat         *z0,
           gfloat         *x1,
           gfloat         *z1)
{
    *x0 = self->origin[0] + (gfloat) p->x0 * self->bake_cs;
    *z0 = self->origin[2] + (gfloat) p->z0 * self->bake_cs;
    *x1 = self->origin[0] + (gfloat) (p->x0 + p->w) * self->bake_cs;
    *z1 = self->origin[2] + (gfloat) (p->z0 + p->h) * self->bake_cs;
}

static gboolean
tag_usable (LrgNavPolyMesh       *self,
            guint32               tag,
            LrgNavPolyFilterFunc  filter,
            gpointer              user_data,
            GHashTable           *cache)
{
    gpointer cached;
    gboolean ok;

    if (tag == 0)
        return TRUE;
    if (g_hash_table_contains (self->disabled_tags, GUINT_TO_POINTER (tag)))
        return FALSE;
    if (filter == NULL)
        return TRUE;
    if (cache != NULL && g_hash_table_lookup_extended (cache, GUINT_TO_POINTER (tag), NULL, &cached))
        return GPOINTER_TO_INT (cached);
    ok = filter (tag, user_data) ? TRUE : FALSE;
    if (cache != NULL)
        g_hash_table_insert (cache, GUINT_TO_POINTER (tag), GINT_TO_POINTER (ok));
    return ok;
}

static gboolean
poly_usable (LrgNavPolyMesh       *self,
             guint                 poly,
             LrgNavPolyFilterFunc  filter,
             gpointer              user_data,
             GHashTable           *cache)
{
    const Poly *p = &g_array_index (self->polys, Poly, poly);

    return p->enabled && tag_usable (self, p->tag, filter, user_data, cache);
}

/*
 * nearest:
 * Every usable polygon with a cell in the columns of the box whose
 * floor lies within the box's height: the point of its rectangle
 * nearest to @point in X/Z, at the floor height there; the nearest in
 * 3D wins.
 */
static gboolean
nearest (LrgNavPolyMesh       *self,
         const gfloat         *point,
         const gfloat         *extents,
         guint                 region,
         LrgNavPolyFilterFunc  filter,
         gpointer              user_data,
         gfloat               *out,
         guint                *out_poly)
{
    gint32 x0;
    gint32 z0;
    gint32 x1;
    gint32 z1;
    gint32 x;
    gint32 z;
    guint best;
    gfloat best_d;
    gfloat best_p[3];
    gint32 last_poly;

    if (!self->baked || !finite3 (point) || !finite3 (extents))
        return FALSE;
    if (!column_of (self, point[0] - fabsf (extents[0]), point[2] - fabsf (extents[2]), &x0, &z0) ||
        !column_of (self, point[0] + fabsf (extents[0]), point[2] + fabsf (extents[2]), &x1, &z1))
        return FALSE;
    x0 = MAX (x0, 0);
    z0 = MAX (z0, 0);
    x1 = MIN (x1, self->width - 1);
    z1 = MIN (z1, self->depth - 1);
    best = LRG_NAV_POLY_MESH_INVALID;
    best_d = G_MAXFLOAT;
    last_poly = -1;
    for (z = z0; z <= z1; z++)
    {
        for (x = x0; x <= x1; x++)
        {
            guint32 column;
            guint32 i;

            column = (guint32) z * (guint32) self->width + (guint32) x;
            for (i = self->col_first[column]; i < self->col_first[column + 1]; i++)
            {
                const Cell *c = &g_array_index (self->cells, Cell, i);
                const Poly *p;
                gfloat rx0, rz0, rx1, rz1;
                gfloat q[3];
                gfloat d;
                gfloat y;

                y = cell_y (self, c->y);
                if (fabsf (y - point[1]) > fabsf (extents[1]))
                    continue;
                if (c->poly == last_poly)
                    continue;
                if (!poly_usable (self, (guint) c->poly, filter, user_data, NULL))
                    continue;
                last_poly = c->poly;
                p = &g_array_index (self->polys, Poly, c->poly);
                if (region != 0 && p->region != region)
                    continue;
                poly_rect (self, p, &rx0, &rz0, &rx1, &rz1);
                q[0] = CLAMP (point[0], rx0 + 1e-3f, rx1 - 1e-3f);
                q[2] = CLAMP (point[2], rz0 + 1e-3f, rz1 - 1e-3f);
                q[1] = poly_height_at (self, (guint) c->poly, q[0], q[2]);
                d = (q[0] - point[0]) * (q[0] - point[0]) + (q[1] - point[1]) * (q[1] - point[1]) +
                    (q[2] - point[2]) * (q[2] - point[2]);
                if (d < best_d)
                {
                    best_d = d;
                    best = (guint) c->poly;
                    memcpy (best_p, q, sizeof (best_p));
                }
            }
        }
    }
    if (best == LRG_NAV_POLY_MESH_INVALID)
        return FALSE;
    if (out != NULL)
        memcpy (out, best_p, sizeof (best_p));
    if (out_poly != NULL)
        *out_poly = best;
    return TRUE;
}

/*
 * nearest_banded:
 * nearest() for @n_bands boxes of the same X/Z extent around @point,
 * band i centred at height band_y[i] with half height band_half[i], in
 * one pass over the columns: the answer is the first band (in order)
 * that has a floor, and within it what nearest() of that band's box
 * would return (the same skips of consecutive cells of one polygon,
 * band by band, and the same first-best tie rule).
 */
static gboolean
nearest_banded (LrgNavPolyMesh *self,
                const gfloat   *point,
                gfloat          extent,
                const gfloat   *band_y,
                const gfloat   *band_half,
                guint           n_bands,
                gfloat         *out,
                guint          *out_poly,
                guint          *out_band)
{
    gint32 x0;
    gint32 z0;
    gint32 x1;
    gint32 z1;
    gint32 x;
    gint32 z;
    guint best[LRG_NAV_POLY_MESH_MAX_BANDS];
    gfloat best_d[LRG_NAV_POLY_MESH_MAX_BANDS];
    gfloat best_p[LRG_NAV_POLY_MESH_MAX_BANDS][3];
    gint32 last_poly[LRG_NAV_POLY_MESH_MAX_BANDS];
    gfloat lo = G_MAXFLOAT;
    gfloat hi = -G_MAXFLOAT;
    guint b;

    if (!self->baked || !finite3 (point) || !isfinite (extent))
        return FALSE;
    for (b = 0; b < n_bands; b++)
    {
        if (!isfinite (band_y[b]) || !isfinite (band_half[b]))
            return FALSE;
        best[b] = LRG_NAV_POLY_MESH_INVALID;
        best_d[b] = G_MAXFLOAT;
        last_poly[b] = -1;
        lo = MIN (lo, band_y[b] - fabsf (band_half[b]));
        hi = MAX (hi, band_y[b] + fabsf (band_half[b]));
    }
    if (!column_of (self, point[0] - fabsf (extent), point[2] - fabsf (extent), &x0, &z0) ||
        !column_of (self, point[0] + fabsf (extent), point[2] + fabsf (extent), &x1, &z1))
        return FALSE;
    x0 = MAX (x0, 0);
    z0 = MAX (z0, 0);
    x1 = MIN (x1, self->width - 1);
    z1 = MIN (z1, self->depth - 1);
    for (z = z0; z <= z1; z++)
    {
        for (x = x0; x <= x1; x++)
        {
            guint32 column;
            guint32 i;

            column = (guint32) z * (guint32) self->width + (guint32) x;
            for (i = self->col_first[column]; i < self->col_first[column + 1]; i++)
            {
                const Cell *c = &g_array_index (self->cells, Cell, i);
                const Poly *p = NULL;
                gfloat rx0, rz0, rx1, rz1;
                gfloat q[3];
                gboolean usable = FALSE;
                gboolean placed = FALSE;
                gfloat y;

                y = cell_y (self, c->y);
                /* outside every band: no band looks at this cell */
                if (y < lo || y > hi)
                    continue;
                for (b = 0; b < n_bands; b++)
                {
                    gfloat d;

                    if (fabsf (y - band_y[b]) > fabsf (band_half[b]))
                        continue;
                    if (c->poly == last_poly[b])
                        continue;
                    if (p == NULL)
                    {
                        usable = poly_usable (self, (guint) c->poly, NULL, NULL, NULL);
                        p = &g_array_index (self->polys, Poly, c->poly);
                    }
                    if (!usable)
                        continue;
                    last_poly[b] = c->poly;
                    if (!placed)
                    {
                        poly_rect (self, p, &rx0, &rz0, &rx1, &rz1);
                        q[0] = CLAMP (point[0], rx0 + 1e-3f, rx1 - 1e-3f);
                        q[2] = CLAMP (point[2], rz0 + 1e-3f, rz1 - 1e-3f);
                        q[1] = poly_height_at (self, (guint) c->poly, q[0], q[2]);
                        placed = TRUE;
                    }
                    d = (q[0] - point[0]) * (q[0] - point[0]) + (q[1] - band_y[b]) * (q[1] - band_y[b]) +
                        (q[2] - point[2]) * (q[2] - point[2]);
                    if (d < best_d[b])
                    {
                        best_d[b] = d;
                        best[b] = (guint) c->poly;
                        memcpy (best_p[b], q, sizeof (q));
                    }
                }
            }
        }
    }
    for (b = 0; b < n_bands; b++)
    {
        if (best[b] == LRG_NAV_POLY_MESH_INVALID)
            continue;
        if (out != NULL)
            memcpy (out, best_p[b], sizeof (best_p[b]));
        if (out_poly != NULL)
            *out_poly = best[b];
        if (out_band != NULL)
            *out_band = b;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Links                                                                */

static Link *
link_of (LrgNavPolyMesh *self,
         guint32         ref)
{
    if (ref & LINK_DROP_BIT)
        return &g_array_index (self->drops, Link, ref & ~LINK_DROP_BIT);
    return &g_array_index (self->links, Link, ref);
}

static void
add_link_use (LrgNavPolyMesh *self,
              guint32         poly,
              guint32         ref,
              gboolean        forward)
{
    GArray *uses;
    LinkUse use;

    uses = g_hash_table_lookup (self->link_uses, GUINT_TO_POINTER (poly));
    if (uses == NULL)
    {
        uses = g_array_new (FALSE, FALSE, sizeof (LinkUse));
        g_hash_table_insert (self->link_uses, GUINT_TO_POINTER (poly), uses);
    }
    use.ref = ref;
    use.forward = forward;
    g_array_append_val (uses, use);
}

/* snaps both ends of a link to the mesh and registers it on its polygons */
static void
connect_link (LrgNavPolyMesh *self,
              guint32         ref)
{
    Link *link;
    gfloat extents[3];
    gfloat end_extents[3];
    gfloat a[3];
    gfloat b[3];
    guint pa;
    guint pb;

    link = link_of (self, ref);
    link->connected = FALSE;
    if (!self->baked)
        return;
    extents[0] = extents[2] = self->agent_radius + 2.0f * self->bake_cs;
    extents[1] = MAX (self->max_climb, self->bake_ch * 2.0f);
    memcpy (end_extents, extents, sizeof (end_extents));
    end_extents[1] = extents[1] * 2.0f;
    if (!nearest (self, link->start, extents, 0, NULL, NULL, a, &pa) ||
        !nearest (self, link->end, end_extents, 0, NULL, NULL, b, &pb))
        return;
    link->poly_a = pa;
    link->poly_b = pb;
    link->connected = TRUE;
    add_link_use (self, pa, ref, TRUE);
    if (link->bidirectional)
        add_link_use (self, pb, ref, FALSE);
}

guint
lrg_nav_poly_mesh_add_link (LrgNavPolyMesh *self,
                            const gfloat   *start,
                            const gfloat   *end,
                            gboolean        bidirectional,
                            gfloat          cost,
                            guint32         tag)
{
    Link link;
    guint id;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), LRG_NAV_POLY_MESH_INVALID);
    g_return_val_if_fail (start != NULL && end != NULL, LRG_NAV_POLY_MESH_INVALID);

    if (!finite3 (start) || !finite3 (end) || !isfinite (cost) || cost < 0.0f)
        return LRG_NAV_POLY_MESH_INVALID;
    memset (&link, 0, sizeof (link));
    memcpy (link.start, start, sizeof (link.start));
    memcpy (link.end, end, sizeof (link.end));
    link.bidirectional = bidirectional ? TRUE : FALSE;
    link.cost = cost;
    link.tag = tag;
    link.enabled = TRUE;
    link.poly_a = link.poly_b = LRG_NAV_POLY_MESH_INVALID;
    g_array_append_val (self->links, link);
    id = self->links->len - 1;
    if (self->baked)
    {
        connect_link (self, id);
        if (link_of (self, id)->connected)
        {
            /* regions: a new link may join two of them */
            Link *l = link_of (self, id);
            guint ra = g_array_index (self->polys, Poly, l->poly_a).region;
            guint rb = g_array_index (self->polys, Poly, l->poly_b).region;
            guint i;

            if (ra != rb)
            {
                for (i = 0; i < self->polys->len; i++)
                {
                    Poly *p = &g_array_index (self->polys, Poly, i);

                    if (p->region == rb)
                        p->region = ra;
                }
            }
        }
    }
    return id;
}

/* ------------------------------------------------------------------ */
/* Bake: heightfield                                                    */

typedef struct
{
    gint32 smin;
    gint32 smax;
    gint32 next;
    gfloat top;         /* the real height of the span's top */
    guint8 walkable;
} HfSpan;

typedef struct
{
    gint32   width;
    gint32   depth;
    gint32  *heads;
    GArray  *pool;
    gint32   merge;      /* tops within this many layers merge their flag */
} Heightfield;

static void
hf_add_span (Heightfield *hf,
             gint32       x,
             gint32       z,
             gint32       smin,
             gint32       smax,
             gfloat       top,
             guint8       walkable)
{
    gint32 column;
    gint32 prev;
    gint32 cur;
    HfSpan span;

    /*
     * walk the sorted column, absorbing every overlapping span; @prev is
     * the pool index of the span before the insertion point (-1: the
     * column head), an index because appending may move the pool
     */
    column = z * hf->width + x;
    prev = -1;
    cur = hf->heads[column];
    while (cur >= 0)
    {
        HfSpan *c = &g_array_index (hf->pool, HfSpan, cur);
        gint32 next;

        if (c->smin > smax)
            break;
        if (c->smax < smin)
        {
            prev = cur;
            cur = c->next;
            continue;
        }
        /* overlap: the flag of the higher top, both when they meet */
        if (ABS (c->smax - smax) <= hf->merge)
            walkable = walkable | c->walkable;
        else if (c->smax > smax)
            walkable = c->walkable;
        smin = MIN (smin, c->smin);
        smax = MAX (smax, c->smax);
        top = MAX (top, c->top);
        /* unlink c (its slot is simply abandoned in the pool) */
        next = c->next;
        if (prev < 0)
            hf->heads[column] = next;
        else
            g_array_index (hf->pool, HfSpan, prev).next = next;
        cur = next;
    }
    span.smin = smin;
    span.smax = smax;
    span.top = top;
    span.walkable = walkable;
    span.next = cur;
    g_array_append_val (hf->pool, span);
    if (prev < 0)
        hf->heads[column] = (gint32) hf->pool->len - 1;
    else
        g_array_index (hf->pool, HfSpan, prev).next = (gint32) hf->pool->len - 1;
}

/*
 * divide:
 * Splits the convex polygon @in (@n_in corners, xyz) by the line where
 * coordinate @axis equals @at: @below gets the part with smaller
 * coordinates, @above the rest.
 */
static void
divide (const gfloat *in,
        gint          n_in,
        gfloat       *below,
        gint         *n_below,
        gfloat       *above,
        gint         *n_above,
        gfloat        at,
        gint          axis)
{
    gfloat d[CLIP_VERTS];
    gint i;
    gint j;
    gint nb;
    gint na;

    nb = 0;
    na = 0;
    for (i = 0; i < n_in; i++)
        d[i] = at - in[i * 3 + axis];
    for (i = 0, j = n_in - 1; i < n_in; j = i, i++)
    {
        if ((d[j] > 0.0f && d[i] < 0.0f) || (d[j] < 0.0f && d[i] > 0.0f))
        {
            gfloat s = d[j] / (d[j] - d[i]);
            gint k;

            for (k = 0; k < 3; k++)
            {
                gfloat v = in[j * 3 + k] + (in[i * 3 + k] - in[j * 3 + k]) * s;

                if (nb < CLIP_VERTS)
                    below[nb * 3 + k] = v;
                if (na < CLIP_VERTS)
                    above[na * 3 + k] = v;
            }
            nb = MIN (nb + 1, CLIP_VERTS);
            na = MIN (na + 1, CLIP_VERTS);
        }
        if (d[i] >= 0.0f && nb < CLIP_VERTS)
        {
            memcpy (below + nb * 3, in + i * 3, sizeof (gfloat) * 3);
            nb++;
        }
        if (d[i] <= 0.0f && na < CLIP_VERTS)
        {
            memcpy (above + na * 3, in + i * 3, sizeof (gfloat) * 3);
            na++;
        }
    }
    *n_below = nb;
    *n_above = na;
}

static void
rasterise_triangle (Heightfield  *hf,
                    const gfloat *origin,
                    gfloat        cs,
                    gfloat        ch,
                    gint32        layers,
                    const gfloat *t,
                    guint8        walkable)
{
    gfloat in[CLIP_VERTS * 3];
    gfloat row[CLIP_VERTS * 3];
    gfloat rest[CLIP_VERTS * 3];
    gfloat cell[CLIP_VERTS * 3];
    gfloat rest_row[CLIP_VERTS * 3];
    gfloat tmin[3];
    gfloat tmax[3];
    gint n_in;
    gint32 z0;
    gint32 z1;
    gint32 z;
    gint k;

    for (k = 0; k < 3; k++)
    {
        tmin[k] = MIN (t[k], MIN (t[3 + k], t[6 + k]));
        tmax[k] = MAX (t[k], MAX (t[3 + k], t[6 + k]));
    }
    z0 = (gint32) floorf ((tmin[2] - origin[2]) / cs);
    z1 = (gint32) floorf ((tmax[2] - origin[2]) / cs);
    z0 = CLAMP (z0, 0, hf->depth - 1);
    z1 = CLAMP (z1, 0, hf->depth - 1);
    memcpy (in, t, sizeof (gfloat) * 9);
    n_in = 3;
    for (z = z0; z <= z1; z++)
    {
        gint n_row;
        gint n_rest;
        gfloat rmin_x;
        gfloat rmax_x;
        gint32 x0;
        gint32 x1;
        gint32 x;
        gint i;

        divide (in, n_in, row, &n_row, rest, &n_rest, origin[2] + (gfloat) (z + 1) * cs, 2);
        memcpy (in, rest, sizeof (gfloat) * 3 * (gsize) n_rest);
        n_in = n_rest;
        if (n_row < 3)
            continue;
        rmin_x = row[0];
        rmax_x = row[0];
        for (i = 1; i < n_row; i++)
        {
            rmin_x = MIN (rmin_x, row[i * 3]);
            rmax_x = MAX (rmax_x, row[i * 3]);
        }
        x0 = CLAMP ((gint32) floorf ((rmin_x - origin[0]) / cs), 0, hf->width - 1);
        x1 = CLAMP ((gint32) floorf ((rmax_x - origin[0]) / cs), 0, hf->width - 1);
        for (x = x0; x <= x1; x++)
        {
            gint n_cell;
            gint n_rest_row;
            gfloat ymin;
            gfloat ymax;
            gint32 smin;
            gint32 smax;

            divide (row, n_row, cell, &n_cell, rest_row, &n_rest_row,
                    origin[0] + (gfloat) (x + 1) * cs, 0);
            memcpy (row, rest_row, sizeof (gfloat) * 3 * (gsize) n_rest_row);
            n_row = n_rest_row;
            if (n_cell < 3)
                continue;
            ymin = cell[1];
            ymax = cell[1];
            for (i = 1; i < n_cell; i++)
            {
                ymin = MIN (ymin, cell[i * 3 + 1]);
                ymax = MAX (ymax, cell[i * 3 + 1]);
            }
            smin = (gint32) floorf ((ymin - origin[1]) / ch);
            smax = (gint32) ceilf ((ymax - origin[1]) / ch);
            smin = CLAMP (smin, 0, layers);
            smax = CLAMP (smax, 0, layers);
            if (smax <= smin)
                smax = smin + 1;
            hf_add_span (hf, x, z, smin, smax, ymax, walkable);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bake: open spans                                                     */

typedef struct
{
    gint32  y;
    gint32  h;
    gfloat  fy;         /* the floor's real height (steps are measured on it) */
    gint32  con[4];
    guint32 tag;
    gint32  poly;
    guint16 dist;
    guint8  keep;       /* walkable and not eroded */
    guint8  walk;       /* a walkable floor with head room */
} OSpan;

typedef struct
{
    gint32   width;
    gint32   depth;
    guint32 *first;     /* width * depth + 1 */
    GArray  *spans;     /* OSpan */
} Open;

static gboolean
open_connected (const Open *open,
                guint32     s,
                gint        d,
                guint32    *t)
{
    const OSpan *a = &g_array_index (open->spans, OSpan, s);

    if (a->con[d] < 0)
        return FALSE;
    *t = (guint32) a->con[d];
    return TRUE;
}

/* a kept neighbour: connected, not eroded */
static gboolean
kept_neighbour (const Open *open,
                guint32     s,
                gint        d,
                guint32    *t)
{
    if (!open_connected (open, s, d, t))
        return FALSE;
    return g_array_index (open->spans, OSpan, *t).keep;
}

static void
build_open (Open          *open,
            Heightfield   *hf,
            gint32         height_cells,
            gfloat         max_climb)
{
    gint32 x;
    gint32 z;
    guint32 n_columns;
    guint32 column;

    n_columns = (guint32) hf->width * (guint32) hf->depth;
    open->width = hf->width;
    open->depth = hf->depth;
    open->first = g_new0 (guint32, n_columns + 1);
    open->spans = g_array_new (FALSE, FALSE, sizeof (OSpan));

    /*
     * spans of free space above every solid span; only walkable floors
     * with head room are walked on, the others tell drops over cliff
     * faces from walls
     */
    for (column = 0; column < n_columns; column++)
    {
        gint32 cur;

        open->first[column] = open->spans->len;
        for (cur = hf->heads[column]; cur >= 0; )
        {
            const HfSpan *s = &g_array_index (hf->pool, HfSpan, cur);
            gint32 ceiling;

            ceiling = s->next >= 0 ? g_array_index (hf->pool, HfSpan, s->next).smin : G_MAXINT32 / 2;
            {
                OSpan o;

                memset (&o, 0, sizeof (o));
                o.y = s->smax;
                o.h = ceiling - s->smax;
                o.fy = s->top;
                o.con[0] = o.con[1] = o.con[2] = o.con[3] = -1;
                o.poly = -1;
                o.walk = s->walkable && ceiling - s->smax >= height_cells;
                o.keep = o.walk;
                g_array_append_val (open->spans, o);
            }
            cur = s->next;
        }
    }
    open->first[n_columns] = open->spans->len;

    /* neighbours: the floor within the climb with the most overlap */
    for (z = 0; z < hf->depth; z++)
    {
        for (x = 0; x < hf->width; x++)
        {
            guint32 i;

            column = (guint32) z * (guint32) hf->width + (guint32) x;
            for (i = open->first[column]; i < open->first[column + 1]; i++)
            {
                OSpan *s = &g_array_index (open->spans, OSpan, i);
                gint d;

                if (!s->walk)
                    continue;
                for (d = 0; d < 4; d++)
                {
                    gint32 nx = x + dir_x[d];
                    gint32 nz = z + dir_z[d];
                    guint32 ncol;
                    guint32 j;
                    gfloat best_dy;

                    if (nx < 0 || nz < 0 || nx >= hf->width || nz >= hf->depth)
                        continue;
                    ncol = (guint32) nz * (guint32) hf->width + (guint32) nx;
                    best_dy = G_MAXFLOAT;
                    for (j = open->first[ncol]; j < open->first[ncol + 1]; j++)
                    {
                        const OSpan *t = &g_array_index (open->spans, OSpan, j);
                        gint32 bottom = MAX (s->y, t->y);
                        gint32 top = MIN (s->y + s->h, t->y + t->h);
                        gfloat dy = fabsf (t->fy - s->fy);

                        if (!t->walk)
                            continue;
                        if (top - bottom >= height_cells && dy <= max_climb && dy < best_dy)
                        {
                            best_dy = dy;
                            s->con[d] = (gint32) j;
                        }
                    }
                }
            }
        }
    }
}

/* the chamfer distance transform and the erosion it drives */
static void
erode (Open   *open,
       gint32  radius_cells)
{
    guint32 n;
    guint32 i;
    gint32 x;
    gint32 z;

    n = open->spans->len;
    for (i = 0; i < n; i++)
    {
        OSpan *s = &g_array_index (open->spans, OSpan, i);
        gint d;

        s->dist = 0xffff;
        for (d = 0; d < 4; d++)
            if (s->con[d] < 0)
                s->dist = 0;
    }
    if (radius_cells <= 0)
        return;

#define RELAX(target, source, add) \
    G_STMT_START { \
        guint16 v = (guint16) MIN (0xffff, (gint) g_array_index (open->spans, OSpan, (source)).dist + (add)); \
        if (v < (target)->dist) (target)->dist = v; \
    } G_STMT_END

    /* forward pass: -X (and its -Z), -Z (and its +X) */
    for (z = 0; z < open->depth; z++)
    {
        for (x = 0; x < open->width; x++)
        {
            guint32 column = (guint32) z * (guint32) open->width + (guint32) x;

            for (i = open->first[column]; i < open->first[column + 1]; i++)
            {
                OSpan *s = &g_array_index (open->spans, OSpan, i);
                guint32 a;
                guint32 b;

                if (open_connected (open, i, 0, &a))
                {
                    RELAX (s, a, 2);
                    if (open_connected (open, a, 3, &b))
                        RELAX (s, b, 3);
                }
                if (open_connected (open, i, 3, &a))
                {
                    RELAX (s, a, 2);
                    if (open_connected (open, a, 2, &b))
                        RELAX (s, b, 3);
                }
            }
        }
    }
    /* backward pass: +X (and its +Z), +Z (and its -X) */
    for (z = open->depth - 1; z >= 0; z--)
    {
        for (x = open->width - 1; x >= 0; x--)
        {
            guint32 column = (guint32) z * (guint32) open->width + (guint32) x;

            for (i = open->first[column]; i < open->first[column + 1]; i++)
            {
                OSpan *s = &g_array_index (open->spans, OSpan, i);
                guint32 a;
                guint32 b;

                if (open_connected (open, i, 2, &a))
                {
                    RELAX (s, a, 2);
                    if (open_connected (open, a, 1, &b))
                        RELAX (s, b, 3);
                }
                if (open_connected (open, i, 1, &a))
                {
                    RELAX (s, a, 2);
                    if (open_connected (open, a, 0, &b))
                        RELAX (s, b, 3);
                }
            }
        }
    }
#undef RELAX

    for (i = 0; i < n; i++)
    {
        OSpan *s = &g_array_index (open->spans, OSpan, i);

        if ((gint32) s->dist < radius_cells * 2)
            s->keep = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Bake: polygons and portals                                           */

typedef struct
{
    guint32 poly;
    guint32 side;
    guint32 to;
    gint32  along;
    gfloat  y;
} Boundary;

static gint
compare_boundary (gconstpointer a,
                  gconstpointer b)
{
    const Boundary *x = a;
    const Boundary *y = b;

    if (x->poly != y->poly)
        return x->poly < y->poly ? -1 : 1;
    if (x->side != y->side)
        return x->side < y->side ? -1 : 1;
    if (x->to != y->to)
        return x->to < y->to ? -1 : 1;
    if (x->along != y->along)
        return x->along < y->along ? -1 : 1;
    return 0;
}

/* whether span @t can join the rectangle being grown with @tag */
static gboolean
joinable (const Open *open,
          guint32     t,
          guint32     tag)
{
    const OSpan *s = &g_array_index (open->spans, OSpan, t);

    return s->keep && s->poly < 0 && s->tag == tag;
}

static void
build_polygons (LrgNavPolyMesh *self,
                Open           *open,
                GArray         *polys)
{
    g_autoptr(GArray) row = g_array_new (FALSE, FALSE, sizeof (guint32));
    g_autoptr(GArray) next = g_array_new (FALSE, FALSE, sizeof (guint32));
    g_autoptr(GArray) members = g_array_new (FALSE, FALSE, sizeof (guint32));
    gint32 x;
    gint32 z;

    for (z = 0; z < open->depth; z++)
    {
        for (x = 0; x < open->width; x++)
        {
            guint32 column = (guint32) z * (guint32) open->width + (guint32) x;
            guint32 i;

            for (i = open->first[column]; i < open->first[column + 1]; i++)
            {
                OSpan *seed = &g_array_index (open->spans, OSpan, i);
                guint32 tag;
                guint32 cur;
                guint32 t;
                gint32 w;
                gint32 h;
                guint k;
                Poly poly;
                guint32 id;

                if (!seed->keep || seed->poly >= 0)
                    continue;
                tag = seed->tag;
                id = polys->len;

                /* grow along +X */
                g_array_set_size (row, 0);
                g_array_append_val (row, i);
                cur = i;
                w = 1;
                seed->poly = (gint32) id;   /* reserve while growing */
                while (w < MAX_SIDE && kept_neighbour (open, cur, 2, &t) && joinable (open, t, tag))
                {
                    g_array_index (open->spans, OSpan, t).poly = (gint32) id;
                    g_array_append_val (row, t);
                    cur = t;
                    w++;
                }
                g_array_set_size (members, 0);
                g_array_append_vals (members, row->data, row->len);

                /* grow whole rows along +Z */
                h = 1;
                while (h < MAX_SIDE)
                {
                    gboolean ok = TRUE;

                    g_array_set_size (next, 0);
                    for (k = 0; k < row->len && ok; k++)
                    {
                        guint32 above;

                        if (!kept_neighbour (open, g_array_index (row, guint32, k), 1, &above) ||
                            !joinable (open, above, tag))
                        {
                            ok = FALSE;
                            break;
                        }
                        /* the new row must be connected along +X too */
                        if (k > 0)
                        {
                            guint32 left_of = g_array_index (next, guint32, k - 1);
                            guint32 linked;

                            if (!kept_neighbour (open, left_of, 2, &linked) || linked != above)
                            {
                                ok = FALSE;
                                break;
                            }
                        }
                        g_array_append_val (next, above);
                    }
                    if (!ok)
                        break;
                    for (k = 0; k < next->len; k++)
                        g_array_index (open->spans, OSpan, g_array_index (next, guint32, k)).poly = (gint32) id;
                    g_array_append_vals (members, next->data, next->len);
                    g_array_set_size (row, 0);
                    g_array_append_vals (row, next->data, next->len);
                    h++;
                }

                memset (&poly, 0, sizeof (poly));
                poly.x0 = x;
                poly.z0 = z;
                poly.w = w;
                poly.h = h;
                poly.tag = tag;
                poly.enabled = TRUE;
                poly.ymin = G_MAXFLOAT;
                poly.ymax = -G_MAXFLOAT;
                for (k = 0; k < members->len; k++)
                {
                    gfloat y = g_array_index (open->spans, OSpan, g_array_index (members, guint32, k)).fy;

                    poly.ymin = MIN (poly.ymin, y);
                    poly.ymax = MAX (poly.ymax, y);
                }
                g_array_append_val (polys, poly);
            }
        }
    }
}

static void
build_portals (LrgNavPolyMesh *self,
               Open           *open,
               GArray         *polys,
               GArray         *edges)
{
    g_autoptr(GArray) bounds = g_array_new (FALSE, FALSE, sizeof (Boundary));
    gint32 x;
    gint32 z;
    guint i;
    guint p;

    for (z = 0; z < open->depth; z++)
    {
        for (x = 0; x < open->width; x++)
        {
            guint32 column = (guint32) z * (guint32) open->width + (guint32) x;
            guint32 s;

            for (s = open->first[column]; s < open->first[column + 1]; s++)
            {
                const OSpan *a = &g_array_index (open->spans, OSpan, s);
                gint d;

                if (!a->keep || a->poly < 0)
                    continue;
                for (d = 0; d < 4; d++)
                {
                    guint32 t;
                    const OSpan *b;
                    Boundary bd;

                    if (!kept_neighbour (open, s, d, &t))
                        continue;
                    b = &g_array_index (open->spans, OSpan, t);
                    if (b->poly < 0 || b->poly == a->poly)
                        continue;
                    bd.poly = (guint32) a->poly;
                    bd.side = (guint32) d;
                    bd.to = (guint32) b->poly;
                    bd.along = (d == 0 || d == 2) ? z : x;
                    bd.y = (a->fy + b->fy) * 0.5f;
                    g_array_append_val (bounds, bd);
                }
            }
        }
    }
    g_array_sort (bounds, compare_boundary);
    for (p = 0; p < polys->len; p++)
    {
        g_array_index (polys, Poly, p).first_edge = 0;
        g_array_index (polys, Poly, p).n_edges = 0;
    }

    /* runs of consecutive cells to one neighbour across one side */
    for (i = 0; i < bounds->len; )
    {
        const Boundary *first = &g_array_index (bounds, Boundary, i);
        const Boundary *last = first;
        Poly *poly = &g_array_index (polys, Poly, first->poly);
        guint j = i + 1;
        Edge e;
        gfloat fixed;
        gfloat lo;
        gfloat hi;

        while (j < bounds->len)
        {
            const Boundary *b = &g_array_index (bounds, Boundary, j);

            if (b->poly != first->poly || b->side != first->side || b->to != first->to ||
                b->along != last->along + 1)
                break;
            last = b;
            j++;
        }
        memset (&e, 0, sizeof (e));
        e.to = (gint32) first->to;
        e.side = first->side;
        e.enabled = TRUE;
        lo = (gfloat) first->along * self->bake_cs;
        hi = (gfloat) (last->along + 1) * self->bake_cs;
        switch (first->side)
        {
        case 0:
        case 2:
            fixed = self->origin[0] + (gfloat) (first->side == 0 ? poly->x0 : poly->x0 + poly->w) * self->bake_cs;
            e.a[0] = e.b[0] = fixed;
            e.a[2] = self->origin[2] + lo;
            e.b[2] = self->origin[2] + hi;
            break;
        default:
            fixed = self->origin[2] + (gfloat) (first->side == 3 ? poly->z0 : poly->z0 + poly->h) * self->bake_cs;
            e.a[2] = e.b[2] = fixed;
            e.a[0] = self->origin[0] + lo;
            e.b[0] = self->origin[0] + hi;
            break;
        }
        e.a[1] = first->y;
        e.b[1] = last->y;
        g_array_append_val (edges, e);
        /* the boundaries are sorted by polygon, so are the edges */
        if (poly->n_edges == 0)
            poly->first_edge = edges->len - 1;
        poly->n_edges++;
        i = j;
    }

}

/* ------------------------------------------------------------------ */
/* Bake: drops, regions                                                 */

/*
 * drop_column:
 * Column (@x, @z) seen from a ledge whose floor is @ledge: the highest
 * floor of a polygon below the climb and within @drop with air up to
 * head height over the ledge (the landing, returned), or -1 with
 * *@passable telling whether air at ledge height crosses the column
 * (a cliff face's top, the void) rather than a wall.
 */
static gint32
drop_column (const Open *open,
             gint32      x,
             gint32      z,
             gint32      ledge,
             gint32      climb,
             gint32      height,
             gint32      drop,
             gboolean   *passable)
{
    guint32 column;
    guint32 i;
    gint32 best;

    *passable = FALSE;
    if (x < 0 || z < 0 || x >= open->width || z >= open->depth)
        return -1;
    column = (guint32) z * (guint32) open->width + (guint32) x;
    best = -1;
    for (i = open->first[column]; i < open->first[column + 1]; i++)
    {
        const OSpan *s = &g_array_index (open->spans, OSpan, i);

        if (s->y <= ledge + climb && s->y + s->h >= ledge + height)
            *passable = TRUE;
        if (s->walk && s->poly >= 0 && s->y < ledge - climb && ledge - s->y <= drop &&
            s->y + s->h >= ledge + height)
            best = (gint32) i;
    }
    return best;
}

static void
build_drops (LrgNavPolyMesh *self,
             Open           *open,
             gint32          radius_cells,
             gint32          climb_cells,
             gint32          height_cells,
             gint32          drop_cells)
{
    g_autoptr(GHashTable) seen = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    gint32 x;
    gint32 z;
    gint32 reach;

    reach = radius_cells + 2;
    for (z = 0; z < open->depth; z++)
    {
        for (x = 0; x < open->width; x++)
        {
            guint32 column = (guint32) z * (guint32) open->width + (guint32) x;
            guint32 s;

            for (s = open->first[column]; s < open->first[column + 1]; s++)
            {
                const OSpan *a = &g_array_index (open->spans, OSpan, s);
                gint d;

                for (d = 0; d < 4; d++)
                {
                    gint32 k;
                    gint32 landing;
                    guint32 start;
                    gint32 back;
                    gint64 key;
                    Link link;
                    const OSpan *l;
                    const OSpan *st;
                    gint32 sx;
                    gint32 sz;
                    gint32 lx;
                    gint32 lz;

                    if (!a->walk || a->con[d] >= 0)
                        continue;
                    /* over the void until a floor of a polygon below */
                    landing = -1;
                    lx = x;
                    lz = z;
                    for (k = 1; k <= reach; k++)
                    {
                        gboolean passable;

                        lx = x + dir_x[d] * k;
                        lz = z + dir_z[d] * k;
                        landing = drop_column (open, lx, lz, a->y, climb_cells, height_cells, drop_cells,
                                               &passable);
                        if (landing >= 0 || !passable)
                            break;
                    }
                    if (landing < 0)
                        continue;
                    /* back from the ledge to the first span of a polygon */
                    start = s;
                    sx = x;
                    sz = z;
                    for (back = 0; back <= reach; back++)
                    {
                        guint32 t;

                        if (g_array_index (open->spans, OSpan, start).poly >= 0)
                            break;
                        if (!open_connected (open, start, (d + 2) & 3, &t))
                        {
                            back = reach + 1;
                            break;
                        }
                        start = t;
                        sx -= dir_x[d];
                        sz -= dir_z[d];
                    }
                    if (back > reach)
                        continue;
                    st = &g_array_index (open->spans, OSpan, start);
                    l = &g_array_index (open->spans, OSpan, landing);
                    if (st->poly == l->poly)
                        continue;
                    key = ((gint64) st->poly << 32) | (gint64) (guint32) l->poly;
                    if (g_hash_table_contains (seen, &key))
                        continue;
                    g_hash_table_add (seen, g_memdup2 (&key, sizeof (key)));

                    memset (&link, 0, sizeof (link));
                    link.start[0] = self->origin[0] + ((gfloat) sx + 0.5f) * self->bake_cs;
                    link.start[1] = st->fy;
                    link.start[2] = self->origin[2] + ((gfloat) sz + 0.5f) * self->bake_cs;
                    link.end[0] = self->origin[0] + ((gfloat) lx + 0.5f) * self->bake_cs;
                    link.end[1] = l->fy;
                    link.end[2] = self->origin[2] + ((gfloat) lz + 0.5f) * self->bake_cs;
                    link.cost = 1.0f;
                    link.enabled = TRUE;
                    link.connected = TRUE;
                    link.poly_a = (guint32) st->poly;
                    link.poly_b = (guint32) l->poly;
                    g_array_append_val (self->drops, link);
                }
            }
        }
    }
}

static guint32
uf_find (guint32 *parent,
         guint32  i)
{
    while (parent[i] != i)
    {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void
uf_union (guint32 *parent,
          guint32  a,
          guint32  b)
{
    a = uf_find (parent, a);
    b = uf_find (parent, b);
    if (a != b)
        parent[MAX (a, b)] = MIN (a, b);
}

static void
label_regions (LrgNavPolyMesh *self)
{
    g_autofree guint32 *parent = NULL;
    g_autofree guint32 *label = NULL;
    guint n;
    guint i;

    n = self->polys->len;
    parent = g_new (guint32, MAX (n, 1u));
    label = g_new0 (guint32, MAX (n, 1u));
    for (i = 0; i < n; i++)
        parent[i] = i;
    for (i = 0; i < n; i++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, i);
        guint e;

        for (e = 0; e < p->n_edges; e++)
            uf_union (parent, i, (guint32) g_array_index (self->edges, Edge, p->first_edge + e).to);
    }
    for (i = 0; i < self->links->len; i++)
    {
        const Link *l = &g_array_index (self->links, Link, i);

        if (l->connected)
            uf_union (parent, l->poly_a, l->poly_b);
    }
    for (i = 0; i < self->drops->len; i++)
    {
        const Link *l = &g_array_index (self->drops, Link, i);

        uf_union (parent, l->poly_a, l->poly_b);
    }
    self->n_regions = 0;
    for (i = 0; i < n; i++)
    {
        guint32 root = uf_find (parent, i);

        if (label[root] == 0)
            label[root] = ++self->n_regions;
        g_array_index (self->polys, Poly, i).region = label[root];
    }
}

static void
free_link_uses (gpointer data)
{
    g_array_unref ((GArray *) data);
}

static void
index_links (LrgNavPolyMesh *self)
{
    guint i;

    g_clear_pointer (&self->link_uses, g_hash_table_unref);
    self->link_uses = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_link_uses);
    for (i = 0; i < self->drops->len; i++)
        add_link_use (self, g_array_index (self->drops, Link, i).poly_a, i | LINK_DROP_BIT, TRUE);
    for (i = 0; i < self->links->len; i++)
    {
        Link *l = &g_array_index (self->links, Link, i);

        if (l->connected)
        {
            add_link_use (self, l->poly_a, i, TRUE);
            if (l->bidirectional)
                add_link_use (self, l->poly_b, i, FALSE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bake                                                                 */

gboolean
lrg_nav_poly_mesh_bake (LrgNavPolyMesh  *self,
                        GError         **error)
{
    Heightfield hf;
    Open open;
    gfloat bmin[3];
    gfloat bmax[3];
    gfloat old_origin[3];
    gfloat old_cs;
    gfloat old_ch;
    gfloat min_up;
    gfloat cs;
    gfloat ch;
    gdouble columns;
    gint32 layers;
    gint32 height_cells;
    gint32 climb_cells;
    gint32 radius_cells;
    gint32 drop_cells;
    gint64 started;
    guint n_tri;
    guint i;
    guint k;
    GArray *polys;
    GArray *edges;
    GArray *cells;
    guint32 *col_first;
    guint32 n_columns;
    GArray *old_drops;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    started = g_get_monotonic_time ();
    n_tri = self->walkable->len;
    if (n_tri == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "navigation mesh: no triangles to bake");
        return FALSE;
    }
    cs = self->cell_size;
    ch = self->cell_height;

    /* --- bounds and the column grid --- */
    for (k = 0; k < 3; k++)
    {
        bmin[k] = G_MAXFLOAT;
        bmax[k] = -G_MAXFLOAT;
    }
    for (i = 0; i < n_tri * 9; i += 3)
    {
        const gfloat *v = &g_array_index (self->triangles, gfloat, i);

        for (k = 0; k < 3; k++)
        {
            bmin[k] = MIN (bmin[k], v[k]);
            bmax[k] = MAX (bmax[k], v[k]);
        }
    }
    columns = (floor ((bmax[0] - bmin[0]) / cs) + 1.0) * (floor ((bmax[2] - bmin[2]) / cs) + 1.0);
    if (columns > (gdouble) LRG_NAV_POLY_MESH_MAX_COLUMNS ||
        (bmax[1] - bmin[1]) / ch > (gfloat) (G_MAXINT32 / 4))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                     "navigation mesh: %.0f columns exceed the limit of %u (raise cell-size)",
                     columns, LRG_NAV_POLY_MESH_MAX_COLUMNS);
        return FALSE;
    }
    hf.width = (gint32) floorf ((bmax[0] - bmin[0]) / cs) + 1;
    hf.depth = (gint32) floorf ((bmax[2] - bmin[2]) / cs) + 1;
    layers = (gint32) ceilf ((bmax[1] - bmin[1]) / ch) + 1;
    height_cells = (gint32) ceilf (self->agent_height / ch);
    climb_cells = (gint32) floorf (self->max_climb / ch + 1e-3f);
    /* whole cells, never less than the radius */
    radius_cells = (gint32) ceilf (self->agent_radius / cs - 1e-4f);
    drop_cells = (gint32) floorf (self->max_drop / ch + 1e-3f);
    hf.merge = climb_cells;
    n_columns = (guint32) hf.width * (guint32) hf.depth;
    hf.heads = g_new (gint32, n_columns);
    memset (hf.heads, 0xff, sizeof (gint32) * n_columns);
    hf.pool = g_array_sized_new (FALSE, FALSE, sizeof (HfSpan), n_tri * 2);

    /* --- 1. rasterise --- */
    min_up = cosf (self->max_slope * (gfloat) G_PI / 180.0f);
    for (i = 0; i < n_tri; i++)
    {
        const gfloat *t = &g_array_index (self->triangles, gfloat, i * 9);
        gfloat e1[3];
        gfloat e2[3];
        gfloat n[3];
        gfloat len;
        guint8 walk;

        for (k = 0; k < 3; k++)
        {
            e1[k] = t[3 + k] - t[k];
            e2[k] = t[6 + k] - t[k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        walk = 0;
        if (len > 1e-12f && g_array_index (self->walkable, guint8, i) && fabsf (n[1]) / len >= min_up)
            walk = 1;
        rasterise_triangle (&hf, bmin, cs, ch, layers, t, walk);
    }

    /* --- 2. low obstacles within the climb become walkable --- */
    for (i = 0; i < n_columns; i++)
    {
        gint32 cur;
        gboolean previous_walkable = FALSE;
        gfloat previous_top = 0.0f;

        for (cur = hf.heads[i]; cur >= 0; )
        {
            HfSpan *s = &g_array_index (hf.pool, HfSpan, cur);
            gboolean was = s->walkable;

            if (!s->walkable && previous_walkable && s->top - previous_top <= self->max_climb)
                s->walkable = 1;
            previous_walkable = was;
            previous_top = s->top;
            cur = s->next;
        }
    }

    /* the bake's geometry frame, used by the helpers below (restored on failure) */
    memcpy (old_origin, self->origin, sizeof (old_origin));
    old_cs = self->bake_cs;
    old_ch = self->bake_ch;
    memcpy (self->origin, bmin, sizeof (bmin));
    self->bake_cs = cs;
    self->bake_ch = ch;

    /* --- 3. open spans and their neighbours --- */
    build_open (&open, &hf, height_cells, self->max_climb);
    g_free (hf.heads);
    g_array_unref (hf.pool);

    /* --- 4. erosion, then the area tags --- */
    erode (&open, radius_cells);
    for (k = 0; k < self->boxes->len; k++)
    {
        const AreaBox *box = &g_array_index (self->boxes, AreaBox, k);
        gint32 x0 = CLAMP ((gint32) floorf ((box->min[0] - bmin[0]) / cs), 0, open.width - 1);
        gint32 x1 = CLAMP ((gint32) floorf ((box->max[0] - bmin[0]) / cs), 0, open.width - 1);
        gint32 z0 = CLAMP ((gint32) floorf ((box->min[2] - bmin[2]) / cs), 0, open.depth - 1);
        gint32 z1 = CLAMP ((gint32) floorf ((box->max[2] - bmin[2]) / cs), 0, open.depth - 1);
        gint32 x;
        gint32 z;

        for (z = z0; z <= z1; z++)
        {
            for (x = x0; x <= x1; x++)
            {
                guint32 column = (guint32) z * (guint32) open.width + (guint32) x;
                gfloat cx = bmin[0] + ((gfloat) x + 0.5f) * cs;
                gfloat cz = bmin[2] + ((gfloat) z + 0.5f) * cs;
                guint32 s;

                if (cx < box->min[0] || cx > box->max[0] || cz < box->min[2] || cz > box->max[2])
                    continue;
                for (s = open.first[column]; s < open.first[column + 1]; s++)
                {
                    OSpan *o = &g_array_index (open.spans, OSpan, s);
                    gfloat y = o->fy;

                    if (y >= box->min[1] && y <= box->max[1])
                        o->tag = box->tag;
                }
            }
        }
    }

    /* --- 5. polygons, 6. portals --- */
    polys = g_array_new (FALSE, FALSE, sizeof (Poly));
    build_polygons (self, &open, polys);
    if (polys->len == 0)
    {
        g_array_unref (polys);
        g_free (open.first);
        g_array_unref (open.spans);
        /* the frame belongs to the previous mesh again */
        memcpy (self->origin, old_origin, sizeof (old_origin));
        self->bake_cs = old_cs;
        self->bake_ch = old_ch;
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "navigation mesh: nothing is walkable for this agent");
        return FALSE;
    }
    edges = g_array_new (FALSE, FALSE, sizeof (Edge));
    build_portals (self, &open, polys, edges);

    /* the kept cells by column */
    col_first = g_new (guint32, n_columns + 1);
    cells = g_array_new (FALSE, FALSE, sizeof (Cell));
    for (i = 0; i < n_columns; i++)
    {
        guint32 s;

        col_first[i] = cells->len;
        for (s = open.first[i]; s < open.first[i + 1]; s++)
        {
            const OSpan *o = &g_array_index (open.spans, OSpan, s);
            Cell c;

            if (o->poly < 0)
                continue;
            c.y = o->fy;
            c.poly = o->poly;
            g_array_append_val (cells, c);
        }
    }
    col_first[n_columns] = cells->len;

    /* --- commit: from here on nothing fails --- */
    old_drops = self->drops;
    self->drops = NULL;
    free_bake (self);
    if (old_drops != NULL)
        g_array_unref (old_drops);
    memcpy (self->origin, bmin, sizeof (bmin));
    self->bake_cs = cs;
    self->bake_ch = ch;
    self->width = open.width;
    self->depth = open.depth;
    self->col_first = col_first;
    self->cells = cells;
    self->polys = polys;
    self->edges = edges;
    self->drops = g_array_new (FALSE, FALSE, sizeof (Link));
    self->baked = TRUE;

    /* --- 7. drops, links, regions --- */
    if (drop_cells > climb_cells)
        build_drops (self, &open, radius_cells, climb_cells, height_cells, drop_cells);
    g_free (open.first);
    g_array_unref (open.spans);
    self->link_uses = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_link_uses);
    for (i = 0; i < self->links->len; i++)
        connect_link (self, i);
    index_links (self);
    label_regions (self);
    self->bake_seconds = (gdouble) (g_get_monotonic_time () - started) / (gdouble) G_USEC_PER_SEC;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Input digest and cached bakes                                        */

/* the digest's version: change it when the bake's inputs change */
#define DIGEST_TAG "lrg-nav-poly-mesh-input-1"

static void
digest_u32 (GChecksum *sum,
            guint32    v)
{
    guint32 le = GUINT32_TO_LE (v);

    g_checksum_update (sum, (const guchar *) &le, sizeof (le));
}

/* floats by their bits, little-endian (the same input, the same digest) */
static void
digest_floats (GChecksum    *sum,
               const gfloat *v,
               guint         n)
{
    guint i;

    for (i = 0; i < n; i++)
    {
        guint32 bits;

        memcpy (&bits, &v[i], sizeof (bits));
        digest_u32 (sum, bits);
    }
}

gchar *
lrg_nav_poly_mesh_get_input_digest (LrgNavPolyMesh *self)
{
    g_autoptr(GChecksum) sum = NULL;
    gfloat props[8];
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), NULL);

    sum = g_checksum_new (G_CHECKSUM_SHA256);
    g_checksum_update (sum, (const guchar *) DIGEST_TAG, sizeof (DIGEST_TAG));

    /* the bake properties */
    props[0] = self->cell_size;
    props[1] = self->cell_height;
    props[2] = self->agent_radius;
    props[3] = self->agent_height;
    props[4] = self->max_climb;
    props[5] = self->max_slope;
    props[6] = self->max_drop;
    props[7] = self->corner_margin;
    digest_floats (sum, props, 8);

    /* the triangles and their walkable flags */
    digest_u32 (sum, self->walkable->len);
    digest_floats (sum, (const gfloat *) (gpointer) self->triangles->data, self->triangles->len);
    g_checksum_update (sum, (const guchar *) self->walkable->data, self->walkable->len);

    /* the area boxes */
    digest_u32 (sum, self->boxes->len);
    for (i = 0; i < self->boxes->len; i++)
    {
        const AreaBox *box = &g_array_index (self->boxes, AreaBox, i);

        digest_floats (sum, box->min, 3);
        digest_floats (sum, box->max, 3);
        digest_u32 (sum, box->tag);
    }

    /* the off-mesh links (their connections are the bake's) */
    digest_u32 (sum, self->links->len);
    for (i = 0; i < self->links->len; i++)
    {
        const Link *l = &g_array_index (self->links, Link, i);

        digest_floats (sum, l->start, 3);
        digest_floats (sum, l->end, 3);
        digest_floats (sum, &l->cost, 1);
        digest_u32 (sum, l->tag);
        digest_u32 (sum, l->bidirectional ? 1u : 0u);
        digest_u32 (sum, l->enabled ? 1u : 0u);
    }
    return g_strdup (g_checksum_get_string (sum));
}

gboolean
lrg_nav_poly_mesh_bake_cached (LrgNavPolyMesh       *self,
                               LrgNavPolyMeshCache  *cache,
                               GError              **error)
{
    g_autofree gchar *digest = NULL;
    g_autoptr(GBytes) bytes = NULL;
    gint64 started;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (cache == NULL || LRG_IS_NAV_POLY_MESH_CACHE (cache), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (cache == NULL)
        return lrg_nav_poly_mesh_bake (self, error);

    started = g_get_monotonic_time ();
    digest = lrg_nav_poly_mesh_get_input_digest (self);
    bytes = lrg_nav_poly_mesh_cache_lookup (cache, digest);
    /* a hit: the bake's own bytes, so a load cannot fail; should it, bake */
    if (bytes != NULL && lrg_nav_poly_mesh_load (self, bytes, NULL))
    {
        self->bake_seconds = (gdouble) (g_get_monotonic_time () - started) / (gdouble) G_USEC_PER_SEC;
        return TRUE;
    }

    if (!lrg_nav_poly_mesh_bake (self, error))
        return FALSE;
    g_clear_pointer (&bytes, g_bytes_unref);
    bytes = lrg_nav_poly_mesh_save (self);
    lrg_nav_poly_mesh_cache_insert (cache, digest, bytes);
    self->bake_seconds = (gdouble) (g_get_monotonic_time () - started) / (gdouble) G_USEC_PER_SEC;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Statistics                                                           */

guint
lrg_nav_poly_mesh_get_polygon_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->baked ? self->polys->len : 0;
}

guint
lrg_nav_poly_mesh_get_portal_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->baked ? self->edges->len : 0;
}

guint
lrg_nav_poly_mesh_get_span_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->baked ? self->cells->len : 0;
}

guint
lrg_nav_poly_mesh_get_link_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->links->len + (self->drops != NULL ? self->drops->len : 0);
}

guint
lrg_nav_poly_mesh_get_drop_link_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->drops != NULL ? self->drops->len : 0;
}

guint
lrg_nav_poly_mesh_get_region_count (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);
    return self->baked ? self->n_regions : 0;
}

gdouble
lrg_nav_poly_mesh_get_bake_seconds (LrgNavPolyMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0.0);
    return self->bake_seconds;
}

gboolean
lrg_nav_poly_mesh_get_polygon (LrgNavPolyMesh *self,
                               guint           polygon,
                               gfloat         *min,
                               gfloat         *max,
                               guint32        *tag,
                               guint          *region)
{
    const Poly *p;
    gfloat x0, z0, x1, z1;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);

    if (!self->baked || polygon >= self->polys->len)
        return FALSE;
    p = &g_array_index (self->polys, Poly, polygon);
    poly_rect (self, p, &x0, &z0, &x1, &z1);
    if (min != NULL)
    {
        min[0] = x0;
        min[1] = p->ymin;
        min[2] = z0;
    }
    if (max != NULL)
    {
        max[0] = x1;
        max[1] = p->ymax;
        max[2] = z1;
    }
    if (tag != NULL)
        *tag = p->tag;
    if (region != NULL)
        *region = p->region;
    return TRUE;
}

gboolean
lrg_nav_poly_mesh_find_nearest (LrgNavPolyMesh *self,
                                const gfloat   *point,
                                const gfloat   *extents,
                                gfloat         *out,
                                guint          *polygon)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (point != NULL && extents != NULL, FALSE);

    return nearest (self, point, extents, 0, NULL, NULL, out, polygon);
}

gboolean
lrg_nav_poly_mesh_find_nearest_banded (LrgNavPolyMesh *self,
                                       const gfloat   *point,
                                       gfloat          extent,
                                       const gfloat   *band_y,
                                       const gfloat   *band_half,
                                       guint           n_bands,
                                       gfloat         *out,
                                       guint          *polygon,
                                       guint          *band)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (point != NULL, FALSE);
    g_return_val_if_fail (n_bands >= 1 && n_bands <= LRG_NAV_POLY_MESH_MAX_BANDS, FALSE);
    g_return_val_if_fail (band_y != NULL && band_half != NULL, FALSE);

    return nearest_banded (self, point, extent, band_y, band_half, n_bands, out, polygon, band);
}

gboolean
lrg_nav_poly_mesh_find_nearest_in_region (LrgNavPolyMesh *self,
                                          const gfloat   *point,
                                          const gfloat   *extents,
                                          guint           region,
                                          gfloat         *out,
                                          guint          *polygon)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (point != NULL && extents != NULL, FALSE);

    return nearest (self, point, extents, region, NULL, NULL, out, polygon);
}

guint
lrg_nav_poly_mesh_get_polygon_region (LrgNavPolyMesh *self,
                                      guint           polygon)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), 0);

    if (!self->baked || polygon >= self->polys->len)
        return 0;
    return g_array_index (self->polys, Poly, polygon).region;
}

/* ------------------------------------------------------------------ */
/* Paths                                                                */

typedef struct
{
    gfloat  f;
    guint32 poly;
} HeapEntry;

static void
heap_push (GArray   *heap,
           gfloat    f,
           guint32   poly)
{
    HeapEntry e;
    guint i;

    e.f = f;
    e.poly = poly;
    g_array_append_val (heap, e);
    i = heap->len - 1;
    while (i > 0)
    {
        guint up = (i - 1) / 2;
        HeapEntry *a = &g_array_index (heap, HeapEntry, up);
        HeapEntry *b = &g_array_index (heap, HeapEntry, i);
        HeapEntry tmp;

        if (a->f <= b->f)
            break;
        tmp = *a;
        *a = *b;
        *b = tmp;
        i = up;
    }
}

static HeapEntry
heap_pop (GArray *heap)
{
    HeapEntry top = g_array_index (heap, HeapEntry, 0);
    guint i = 0;
    guint n;

    g_array_index (heap, HeapEntry, 0) = g_array_index (heap, HeapEntry, heap->len - 1);
    g_array_set_size (heap, heap->len - 1);
    n = heap->len;
    for (;;)
    {
        guint l = i * 2 + 1;
        guint r = l + 1;
        guint m = i;
        HeapEntry tmp;

        if (l < n && g_array_index (heap, HeapEntry, l).f < g_array_index (heap, HeapEntry, m).f)
            m = l;
        if (r < n && g_array_index (heap, HeapEntry, r).f < g_array_index (heap, HeapEntry, m).f)
            m = r;
        if (m == i)
            break;
        tmp = g_array_index (heap, HeapEntry, i);
        g_array_index (heap, HeapEntry, i) = g_array_index (heap, HeapEntry, m);
        g_array_index (heap, HeapEntry, m) = tmp;
        i = m;
    }
    return top;
}

static gfloat
dist3 (const gfloat *a,
       const gfloat *b)
{
    gfloat dx = a[0] - b[0];
    gfloat dy = a[1] - b[1];
    gfloat dz = a[2] - b[2];

    return sqrtf (dx * dx + dy * dy + dz * dz);
}

static void
ensure_scratch (LrgNavPolyMesh *self)
{
    guint n = self->polys->len;

    if (self->scratch_size == n)
        return;
    g_free (self->stamp);
    g_free (self->cost);
    g_free (self->parent);
    g_free (self->parent_via);
    g_free (self->node_pos);
    g_free (self->closed);
    self->stamp = g_new0 (guint32, n);
    self->cost = g_new (gfloat, n);
    self->parent = g_new (gint32, n);
    self->parent_via = g_new (guint32, n);
    self->node_pos = g_new (gfloat, n * 3);
    self->closed = g_new0 (guint8, n);
    self->generation = 0;
    self->scratch_size = n;
}

/* a step of the corridor: the polygon and how it was entered */
#define VIA_START (G_MAXUINT32)
#define VIA_LINK  (0x40000000u)   /* or-ed with a LinkUse index pair */

typedef struct
{
    guint32 poly;
    guint32 via;      /* edge index, VIA_START, or link ref | VIA_LINK flag below */
    gboolean is_link;
    guint32 link_ref;
    gboolean forward;
} Step;

static gfloat
cross2 (const gfloat *o,
        const gfloat *a,
        const gfloat *p)
{
    return (a[0] - o[0]) * (p[2] - o[2]) - (a[2] - o[2]) * (p[0] - o[0]);
}

static gboolean
same2 (const gfloat *a,
       const gfloat *b)
{
    return fabsf (a[0] - b[0]) < 1e-5f && fabsf (a[2] - b[2]) < 1e-5f;
}

static void
append_point (GArray       *out,
              const gfloat *p)
{
    if (out->len >= 3)
    {
        const gfloat *last = &g_array_index (out, gfloat, out->len - 3);

        if (same2 (last, p) && fabsf (last[1] - p[1]) < 1e-4f)
            return;
    }
    g_array_append_vals (out, p, 3);
}

/*
 * funnel:
 * The "simple stupid funnel": @portals holds left/right pairs (six
 * floats) seen in travel direction; the goal closes the list.  Appends
 * the corners after @start, ending with @goal.
 */
static void
funnel (const gfloat *start,
        const gfloat *goal,
        GArray       *portals,
        GArray       *out)
{
    gfloat apex[3];
    gfloat left[3];
    gfloat right[3];
    gint apex_i;
    gint left_i;
    gint right_i;
    gint n;
    gint i;

    n = (gint) (portals->len / 6);
    memcpy (apex, start, sizeof (apex));
    memcpy (left, start, sizeof (left));
    memcpy (right, start, sizeof (right));
    apex_i = left_i = right_i = -1;
    for (i = 0; i <= n; i++)
    {
        const gfloat *pl;
        const gfloat *pr;

        if (i < n)
        {
            pl = &g_array_index (portals, gfloat, i * 6);
            pr = pl + 3;
        }
        else
        {
            pl = goal;
            pr = goal;
        }

        /* the right side narrows */
        if (cross2 (apex, right, pr) >= 0.0f)
        {
            if (same2 (apex, right) || cross2 (apex, left, pr) < 0.0f)
            {
                memcpy (right, pr, sizeof (right));
                right_i = i;
            }
            else
            {
                append_point (out, left);
                memcpy (apex, left, sizeof (apex));
                apex_i = left_i;
                memcpy (right, apex, sizeof (right));
                right_i = apex_i;
                i = apex_i;
                continue;
            }
        }
        /* the left side narrows */
        if (cross2 (apex, left, pl) <= 0.0f)
        {
            if (same2 (apex, left) || cross2 (apex, right, pl) > 0.0f)
            {
                memcpy (left, pl, sizeof (left));
                left_i = i;
            }
            else
            {
                append_point (out, right);
                memcpy (apex, right, sizeof (apex));
                apex_i = right_i;
                memcpy (left, apex, sizeof (left));
                left_i = apex_i;
                i = apex_i;
                continue;
            }
        }
    }
    append_point (out, goal);
}

/* left and right ends of edge @e, seen leaving through its side */
static void
edge_portal (const Edge *e,
             gfloat     *l,
             gfloat     *r)
{
    gfloat n[2];
    gfloat mid[2];
    gfloat c;

    n[0] = (gfloat) dir_x[e->side];
    n[1] = (gfloat) dir_z[e->side];
    mid[0] = (e->a[0] + e->b[0]) * 0.5f;
    mid[1] = (e->a[2] + e->b[2]) * 0.5f;
    c = n[0] * (e->a[2] - mid[1]) - n[1] * (e->a[0] - mid[0]);
    if (c > 0.0f)
    {
        memcpy (l, e->a, sizeof (gfloat) * 3);
        memcpy (r, e->b, sizeof (gfloat) * 3);
    }
    else
    {
        memcpy (l, e->b, sizeof (gfloat) * 3);
        memcpy (r, e->a, sizeof (gfloat) * 3);
    }
}

gfloat *
lrg_nav_poly_mesh_find_path (LrgNavPolyMesh        *self,
                             const gfloat          *start,
                             const gfloat          *goal,
                             const gfloat          *extents,
                             guint                  max_nodes,
                             LrgNavPolyFilterFunc   filter,
                             gpointer               user_data,
                             guint                 *out_expanded,
                             guint                 *n_floats,
                             GError               **error)
{
    g_autoptr(GHashTable) cache = NULL;
    g_autoptr(GArray) heap = NULL;
    g_autoptr(GArray) steps = NULL;
    g_autoptr(GArray) portals = NULL;
    g_autoptr(GArray) out = NULL;
    gfloat s[3];
    gfloat g[3];
    guint ps;
    guint pg;
    guint expanded;
    gboolean found;
    guint32 cur;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), NULL);
    g_return_val_if_fail (start != NULL && goal != NULL && extents != NULL, NULL);
    g_return_val_if_fail (n_floats != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    *n_floats = 0;
    if (out_expanded != NULL)
        *out_expanded = 0;
    cache = g_hash_table_new (g_direct_hash, g_direct_equal);
    if (!nearest (self, start, extents, 0, filter, user_data, s, &ps) ||
        !nearest (self, goal, extents, 0, filter, user_data, g, &pg))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "navigation mesh: start or goal is off the mesh");
        return NULL;
    }
    out = g_array_new (FALSE, FALSE, sizeof (gfloat));
    if (ps == pg)
    {
        append_point (out, s);
        append_point (out, g);
        if (out->len == 3)
            g_array_append_vals (out, g, 3);
        *n_floats = out->len;
        return (gfloat *) g_array_free (g_steal_pointer (&out), FALSE);
    }
    if (g_array_index (self->polys, Poly, ps).region != g_array_index (self->polys, Poly, pg).region)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "navigation mesh: the goal is in another region");
        return NULL;
    }

    /* --- A* over polygons, costs between entry points --- */
    ensure_scratch (self);
    self->generation++;
    if (self->generation == 0)
    {
        memset (self->stamp, 0, sizeof (guint32) * self->scratch_size);
        self->generation = 1;
    }
    heap = g_array_new (FALSE, FALSE, sizeof (HeapEntry));
    self->stamp[ps] = self->generation;
    self->cost[ps] = 0.0f;
    self->parent[ps] = -1;
    self->parent_via[ps] = VIA_START;
    self->closed[ps] = 0;
    memcpy (self->node_pos + ps * 3, s, sizeof (gfloat) * 3);
    heap_push (heap, dist3 (s, g), ps);
    expanded = 0;
    found = FALSE;
    while (heap->len > 0)
    {
        HeapEntry top = heap_pop (heap);
        const Poly *p;
        const gfloat *pos;
        GArray *uses;
        guint e;

        cur = top.poly;
        if (self->closed[cur])
            continue;
        self->closed[cur] = 1;
        if (cur == pg)
        {
            found = TRUE;
            break;
        }
        expanded++;
        if (max_nodes > 0 && expanded > max_nodes)
            break;
        p = &g_array_index (self->polys, Poly, cur);
        pos = self->node_pos + cur * 3;

        /* portals */
        for (e = 0; e < p->n_edges; e++)
        {
            guint32 ei = p->first_edge + e;
            const Edge *edge = &g_array_index (self->edges, Edge, ei);
            guint32 to = (guint32) edge->to;
            gfloat mid[3];
            gfloat c;

            if (!edge->enabled)
                continue;
            if (self->stamp[to] == self->generation && self->closed[to])
                continue;
            if (!poly_usable (self, to, filter, user_data, cache))
                continue;
            mid[0] = (edge->a[0] + edge->b[0]) * 0.5f;
            mid[1] = (edge->a[1] + edge->b[1]) * 0.5f;
            mid[2] = (edge->a[2] + edge->b[2]) * 0.5f;
            c = self->cost[cur] + dist3 (pos, mid);
            if (to == pg)
                c += dist3 (mid, g);
            if (self->stamp[to] != self->generation || c < self->cost[to])
            {
                self->stamp[to] = self->generation;
                self->closed[to] = 0;
                self->cost[to] = c;
                self->parent[to] = (gint32) cur;
                self->parent_via[to] = ei;
                memcpy (self->node_pos + to * 3, mid, sizeof (mid));
                heap_push (heap, c + (to == pg ? 0.0f : dist3 (mid, g)), to);
            }
        }

        /* off-mesh links */
        uses = g_hash_table_lookup (self->link_uses, GUINT_TO_POINTER (cur));
        for (e = 0; uses != NULL && e < uses->len; e++)
        {
            const LinkUse *use = &g_array_index (uses, LinkUse, e);
            const Link *link = link_of (self, use->ref);
            const gfloat *enter = use->forward ? link->start : link->end;
            const gfloat *leave = use->forward ? link->end : link->start;
            guint32 to = use->forward ? link->poly_b : link->poly_a;
            gfloat c;

            if (!link->enabled || !link->connected ||
                !tag_usable (self, link->tag, filter, user_data, cache))
                continue;
            if (self->stamp[to] == self->generation && self->closed[to])
                continue;
            if (!poly_usable (self, to, filter, user_data, cache))
                continue;
            c = self->cost[cur] + dist3 (pos, enter) + dist3 (enter, leave) + link->cost;
            if (to == pg)
                c += dist3 (leave, g);
            if (self->stamp[to] != self->generation || c < self->cost[to])
            {
                self->stamp[to] = self->generation;
                self->closed[to] = 0;
                self->cost[to] = c;
                self->parent[to] = (gint32) cur;
                self->parent_via[to] = VIA_LINK | (use->ref & LINK_DROP_BIT ? 0x20000000u : 0u) |
                                       (use->ref & ~LINK_DROP_BIT) | (use->forward ? 0x10000000u : 0u);
                memcpy (self->node_pos + to * 3, leave, sizeof (gfloat) * 3);
                heap_push (heap, c + (to == pg ? 0.0f : dist3 (leave, g)), to);
            }
        }
    }
    if (out_expanded != NULL)
        *out_expanded = expanded;
    if (!found)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             max_nodes > 0 && expanded > max_nodes
                             ? "navigation mesh: the search ran out of nodes"
                             : "navigation mesh: no path to the goal");
        return NULL;
    }

    /* --- the corridor, start to goal --- */
    steps = g_array_new (FALSE, FALSE, sizeof (Step));
    for (cur = pg; ; cur = (guint32) self->parent[cur])
    {
        Step step;

        memset (&step, 0, sizeof (step));
        step.poly = cur;
        step.via = self->parent_via[cur];
        if (step.via != VIA_START && (step.via & VIA_LINK))
        {
            step.is_link = TRUE;
            step.forward = (step.via & 0x10000000u) != 0;
            step.link_ref = (step.via & 0x0fffffffu) | ((step.via & 0x20000000u) ? LINK_DROP_BIT : 0u);
        }
        g_array_prepend_val (steps, step);
        if (self->parent[cur] < 0)
            break;
    }

    /* --- funnel each stretch between links --- */
    portals = g_array_new (FALSE, FALSE, sizeof (gfloat));
    append_point (out, s);
    {
        gfloat from[3];

        memcpy (from, s, sizeof (from));
        for (i = 1; i < steps->len; i++)
        {
            const Step *step = &g_array_index (steps, Step, i);

            if (step->is_link)
            {
                const Link *link = link_of (self, step->link_ref);
                const gfloat *enter = step->forward ? link->start : link->end;
                const gfloat *leave = step->forward ? link->end : link->start;
                gfloat e_on[3];
                gfloat l_on[3];
                const Step *prev = &g_array_index (steps, Step, i - 1);

                /* the link ends on their polygons' floors */
                memcpy (e_on, enter, sizeof (e_on));
                memcpy (l_on, leave, sizeof (l_on));
                e_on[1] = poly_height_at (self, prev->poly, e_on[0], e_on[2]);
                l_on[1] = poly_height_at (self, step->poly, l_on[0], l_on[2]);
                funnel (from, e_on, portals, out);
                append_point (out, l_on);
                g_array_set_size (portals, 0);
                memcpy (from, l_on, sizeof (from));
            }
            else
            {
                const Edge *edge = &g_array_index (self->edges, Edge, step->via);
                gfloat lr[6];

                edge_portal (edge, lr, lr + 3);
                if (self->corner_margin > 0.0f)
                {
                    gfloat dx = lr[3] - lr[0];
                    gfloat dz = lr[5] - lr[2];
                    gfloat len = sqrtf (dx * dx + dz * dz);
                    gfloat cut = MIN (self->corner_margin, len * 0.5f);
                    guint k;

                    /* both ends towards each other (heights follow) */
                    if (len > 1e-6f)
                    {
                        gfloat f = cut / len;

                        for (k = 0; k < 3; k++)
                        {
                            gfloat a = lr[k];
                            gfloat b = lr[3 + k];

                            lr[k] = a + (b - a) * f;
                            lr[3 + k] = b + (a - b) * f;
                        }
                    }
                }
                g_array_append_vals (portals, lr, 6);
            }
        }
        funnel (from, g, portals, out);
    }
    if (out->len == 3)
        g_array_append_vals (out, g, 3);
    *n_floats = out->len;
    return (gfloat *) g_array_free (g_steal_pointer (&out), FALSE);
}

/* ------------------------------------------------------------------ */
/* Raycast                                                              */

gboolean
lrg_nav_poly_mesh_raycast (LrgNavPolyMesh       *self,
                           const gfloat         *start,
                           const gfloat         *end,
                           const gfloat         *extents,
                           LrgNavPolyFilterFunc  filter,
                           gpointer              user_data,
                           gfloat               *hit_fraction,
                           gfloat               *hit_point)
{
    g_autoptr(GHashTable) cache = NULL;
    gfloat s[3];
    gfloat dx;
    gfloat dz;
    gfloat t;
    guint poly;
    guint steps;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), TRUE);
    g_return_val_if_fail (start != NULL && end != NULL && extents != NULL, TRUE);

    if (hit_fraction != NULL)
        *hit_fraction = 0.0f;
    cache = g_hash_table_new (g_direct_hash, g_direct_equal);
    if (!finite3 (end) || !finite3 (start))
    {
        if (hit_point != NULL)
            memcpy (hit_point, start, sizeof (gfloat) * 3);
        return TRUE;
    }
    /*
     * the start polygon: the one under a probe a millimetre along the
     * line, so a start on a polygon's edge or corner (path corners are)
     * picks the polygon the line enters
     */
    {
        gfloat probe[3];
        gfloat len;
        gfloat x0, z0, x1, z1;

        dx = end[0] - start[0];
        dz = end[2] - start[2];
        len = sqrtf (dx * dx + dz * dz);
        memcpy (probe, start, sizeof (probe));
        if (len > 1e-6f)
        {
            probe[0] += dx / len * MIN (1e-3f, len * 0.5f);
            probe[2] += dz / len * MIN (1e-3f, len * 0.5f);
        }
        if (!nearest (self, probe, extents, 0, filter, user_data, s, &poly))
        {
            if (hit_point != NULL)
                memcpy (hit_point, start, sizeof (gfloat) * 3);
            return TRUE;
        }
        poly_rect (self, &g_array_index (self->polys, Poly, poly), &x0, &z0, &x1, &z1);
        if (start[0] >= x0 - 1e-3f && start[0] <= x1 + 1e-3f && start[2] >= z0 - 1e-3f && start[2] <= z1 + 1e-3f)
        {
            s[0] = start[0];
            s[2] = start[2];
        }
    }
    dx = end[0] - s[0];
    dz = end[2] - s[2];
    t = 0.0f;
    for (steps = 0; steps < 1000000u; steps++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, poly);
        gfloat x0, z0, x1, z1;
        gfloat tx;
        gfloat tz;
        gfloat texit;
        gfloat ex;
        gfloat ez;
        gint sides[2];
        gint n_sides;
        guint next;
        gint k;

        poly_rect (self, p, &x0, &z0, &x1, &z1);
        tx = G_MAXFLOAT;
        tz = G_MAXFLOAT;
        if (dx > 1e-9f)
            tx = (x1 - s[0]) / dx;
        else if (dx < -1e-9f)
            tx = (x0 - s[0]) / dx;
        if (dz > 1e-9f)
            tz = (z1 - s[2]) / dz;
        else if (dz < -1e-9f)
            tz = (z0 - s[2]) / dz;
        texit = MIN (tx, tz);
        if (texit >= 1.0f)
        {
            if (hit_fraction != NULL)
                *hit_fraction = 1.0f;
            if (hit_point != NULL)
            {
                hit_point[0] = end[0];
                hit_point[2] = end[2];
                hit_point[1] = poly_height_at (self, poly, end[0], end[2]);
            }
            return FALSE;
        }
        texit = MAX (texit, t);
        ex = s[0] + dx * texit;
        ez = s[2] + dz * texit;
        n_sides = 0;
        if (fabsf (tx - texit) < 1e-6f)
            sides[n_sides++] = dx > 0.0f ? 2 : 0;
        if (fabsf (tz - texit) < 1e-6f)
            sides[n_sides++] = dz > 0.0f ? 1 : 3;
        next = LRG_NAV_POLY_MESH_INVALID;
        for (k = 0; k < n_sides && next == LRG_NAV_POLY_MESH_INVALID; k++)
        {
            guint e;

            for (e = 0; e < p->n_edges; e++)
            {
                const Edge *edge = &g_array_index (self->edges, Edge, p->first_edge + e);
                gfloat along;
                gfloat lo;
                gfloat hi;

                if ((gint) edge->side != sides[k] || !edge->enabled)
                    continue;
                along = (sides[k] == 0 || sides[k] == 2) ? ez : ex;
                lo = (sides[k] == 0 || sides[k] == 2) ? MIN (edge->a[2], edge->b[2]) : MIN (edge->a[0], edge->b[0]);
                hi = (sides[k] == 0 || sides[k] == 2) ? MAX (edge->a[2], edge->b[2]) : MAX (edge->a[0], edge->b[0]);
                if (along < lo - 1e-4f || along > hi + 1e-4f)
                    continue;
                if (!poly_usable (self, (guint) edge->to, filter, user_data, cache))
                    continue;
                next = (guint) edge->to;
                break;
            }
        }
        if (next == LRG_NAV_POLY_MESH_INVALID)
        {
            if (hit_fraction != NULL)
                *hit_fraction = texit;
            if (hit_point != NULL)
            {
                hit_point[0] = ex;
                hit_point[2] = ez;
                hit_point[1] = poly_height_at (self, poly, ex - dx * 1e-4f, ez - dz * 1e-4f);
            }
            return TRUE;
        }
        poly = next;
        t = texit + 1e-6f;
    }
    if (hit_fraction != NULL)
        *hit_fraction = t;
    if (hit_point != NULL)
        memcpy (hit_point, s, sizeof (gfloat) * 3);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Random points                                                        */

gboolean
lrg_nav_poly_mesh_random_point (LrgNavPolyMesh *self,
                                const gfloat   *from,
                                const gfloat   *extents,
                                guint32         seed,
                                gfloat         *out)
{
    g_autoptr(GRand) rand = NULL;
    static const gfloat one_metre[3] = { 1.0f, 1.0f, 1.0f };
    guint region;
    gdouble total;
    gdouble pick;
    guint chosen;
    guint i;
    gint32 cx;
    gint32 cz;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (out != NULL, FALSE);

    if (!self->baked)
        return FALSE;
    region = 0;
    if (from != NULL)
    {
        guint poly;

        if (!nearest (self, from, extents != NULL ? extents : one_metre, 0, NULL, NULL, NULL, &poly))
            return FALSE;
        region = g_array_index (self->polys, Poly, poly).region;
    }
    total = 0.0;
    for (i = 0; i < self->polys->len; i++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, i);

        if ((region == 0 || p->region == region) && poly_usable (self, i, NULL, NULL, NULL))
            total += (gdouble) p->w * (gdouble) p->h;
    }
    if (total <= 0.0)
        return FALSE;
    rand = g_rand_new_with_seed (seed);
    pick = g_rand_double_range (rand, 0.0, total);
    chosen = LRG_NAV_POLY_MESH_INVALID;
    for (i = 0; i < self->polys->len; i++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, i);
        gdouble area;

        if ((region != 0 && p->region != region) || !poly_usable (self, i, NULL, NULL, NULL))
            continue;
        area = (gdouble) p->w * (gdouble) p->h;
        chosen = i;
        if (pick < area)
            break;
        pick -= area;
    }
    {
        const Poly *p = &g_array_index (self->polys, Poly, chosen);

        cx = p->x0 + g_rand_int_range (rand, 0, p->w);
        cz = p->z0 + g_rand_int_range (rand, 0, p->h);
        out[0] = self->origin[0] + ((gfloat) cx + (gfloat) g_rand_double (rand)) * self->bake_cs;
        out[2] = self->origin[2] + ((gfloat) cz + (gfloat) g_rand_double (rand)) * self->bake_cs;
        out[1] = poly_height (self, chosen, cx, cz);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Switches                                                             */

gboolean
lrg_nav_poly_mesh_set_polygon_enabled (LrgNavPolyMesh *self,
                                       guint           polygon,
                                       gboolean        enabled)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);

    if (!self->baked || polygon >= self->polys->len)
        return FALSE;
    g_array_index (self->polys, Poly, polygon).enabled = enabled ? TRUE : FALSE;
    return TRUE;
}

gboolean
lrg_nav_poly_mesh_set_link_enabled (LrgNavPolyMesh *self,
                                    guint           link,
                                    gboolean        enabled)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);

    if (link >= self->links->len)
        return FALSE;
    g_array_index (self->links, Link, link).enabled = enabled ? TRUE : FALSE;
    return TRUE;
}

gboolean
lrg_nav_poly_mesh_get_link_connected (LrgNavPolyMesh *self,
                                      guint           link)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);

    if (link >= self->links->len)
        return FALSE;
    return g_array_index (self->links, Link, link).connected;
}

void
lrg_nav_poly_mesh_set_tag_enabled (LrgNavPolyMesh *self,
                                   guint32         tag,
                                   gboolean        enabled)
{
    g_return_if_fail (LRG_IS_NAV_POLY_MESH (self));

    if (tag == 0)
        return;
    if (enabled)
        g_hash_table_remove (self->disabled_tags, GUINT_TO_POINTER (tag));
    else
        g_hash_table_add (self->disabled_tags, GUINT_TO_POINTER (tag));
}

gboolean
lrg_nav_poly_mesh_get_tag_enabled (LrgNavPolyMesh *self,
                                   guint32         tag)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);

    return tag == 0 || !g_hash_table_contains (self->disabled_tags, GUINT_TO_POINTER (tag));
}

gboolean
lrg_nav_poly_mesh_block_portal (LrgNavPolyMesh *self,
                                const gfloat   *from,
                                const gfloat   *toward)
{
    static const gfloat box[3] = { 0.75f, 1.0f, 0.75f };
    const Poly *p;
    guint poly;
    guint e;
    gint best;
    gfloat best_dot;
    gfloat dir[2];
    gfloat len;
    Edge *edge;
    const Poly *q;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (from != NULL && toward != NULL, FALSE);

    if (!nearest (self, from, box, 0, NULL, NULL, NULL, &poly))
        return FALSE;
    dir[0] = toward[0] - from[0];
    dir[1] = toward[2] - from[2];
    len = sqrtf (dir[0] * dir[0] + dir[1] * dir[1]);
    if (len < 1e-5f)
        return FALSE;
    dir[0] /= len;
    dir[1] /= len;
    p = &g_array_index (self->polys, Poly, poly);
    best = -1;
    best_dot = -2.0f;
    for (e = 0; e < p->n_edges; e++)
    {
        const Edge *c = &g_array_index (self->edges, Edge, p->first_edge + e);
        gfloat mx = (c->a[0] + c->b[0]) * 0.5f - from[0];
        gfloat mz = (c->a[2] + c->b[2]) * 0.5f - from[2];
        gfloat ml = sqrtf (mx * mx + mz * mz);
        gfloat dot;

        if (!c->enabled || ml < 1e-5f)
            continue;
        dot = (mx * dir[0] + mz * dir[1]) / ml;
        if (dot > best_dot)
        {
            best_dot = dot;
            best = (gint) (p->first_edge + e);
        }
    }
    if (best < 0)
        return FALSE;
    edge = &g_array_index (self->edges, Edge, best);
    edge->enabled = FALSE;
    /* the reverse portal: same segment, the other polygon */
    q = &g_array_index (self->polys, Poly, edge->to);
    for (e = 0; e < q->n_edges; e++)
    {
        Edge *r = &g_array_index (self->edges, Edge, q->first_edge + e);

        if (r->to == (gint32) poly && r->side == ((edge->side + 2) & 3u))
        {
            gfloat lo = MAX ((r->side & 1u) ? MIN (r->a[0], r->b[0]) : MIN (r->a[2], r->b[2]),
                             (edge->side & 1u) ? MIN (edge->a[0], edge->b[0]) : MIN (edge->a[2], edge->b[2]));
            gfloat hi = MIN ((r->side & 1u) ? MAX (r->a[0], r->b[0]) : MAX (r->a[2], r->b[2]),
                             (edge->side & 1u) ? MAX (edge->a[0], edge->b[0]) : MAX (edge->a[2], edge->b[2]));

            if (hi > lo)
                r->enabled = FALSE;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* OBJ                                                                  */

gchar *
lrg_nav_poly_mesh_to_obj (LrgNavPolyMesh *self)
{
    GString *obj;
    guint i;
    guint vertex;
    guint r;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), NULL);

    obj = g_string_new ("# libregnum navigation mesh\n");
    if (!self->baked)
        return g_string_free (obj, FALSE);
    g_string_append_printf (obj, "# %u polygons, %u portals, %u links, %u regions\n",
                            self->polys->len, self->edges->len,
                            self->links->len + self->drops->len, self->n_regions);
    /* vertices: four per polygon, in polygon order */
    for (i = 0; i < self->polys->len; i++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, i);
        gfloat x0, z0, x1, z1;

        poly_rect (self, p, &x0, &z0, &x1, &z1);
        g_string_append_printf (obj, "v %.4f %.4f %.4f\n", x0,
                                poly_height (self, i, p->x0, p->z0), z0);
        g_string_append_printf (obj, "v %.4f %.4f %.4f\n", x1,
                                poly_height (self, i, p->x0 + p->w - 1, p->z0), z0);
        g_string_append_printf (obj, "v %.4f %.4f %.4f\n", x1,
                                poly_height (self, i, p->x0 + p->w - 1, p->z0 + p->h - 1), z1);
        g_string_append_printf (obj, "v %.4f %.4f %.4f\n", x0,
                                poly_height (self, i, p->x0, p->z0 + p->h - 1), z1);
    }
    /* faces grouped by region, wound counter-clockwise seen from above */
    for (r = 1; r <= self->n_regions; r++)
    {
        g_string_append_printf (obj, "g region_%u\n", r);
        for (i = 0; i < self->polys->len; i++)
        {
            if (g_array_index (self->polys, Poly, i).region != r)
                continue;
            g_string_append_printf (obj, "f %u %u %u %u\n", i * 4 + 1, i * 4 + 4, i * 4 + 3, i * 4 + 2);
        }
    }
    /* links as lines */
    vertex = self->polys->len * 4;
    if (self->links->len + self->drops->len > 0)
        g_string_append (obj, "g links\n");
    for (i = 0; i < self->links->len + self->drops->len; i++)
    {
        const Link *l = i < self->links->len ? &g_array_index (self->links, Link, i)
                                             : &g_array_index (self->drops, Link, i - self->links->len);

        g_string_append_printf (obj, "v %.4f %.4f %.4f\nv %.4f %.4f %.4f\nl %u %u\n",
                                l->start[0], l->start[1], l->start[2],
                                l->end[0], l->end[1], l->end[2], vertex + 1, vertex + 2);
        vertex += 2;
    }
    return g_string_free (obj, FALSE);
}

/* ------------------------------------------------------------------ */
/* Save and load                                                        */

static void
put_u32 (GByteArray *b,
         guint32     v)
{
    guint32 le = GUINT32_TO_LE (v);

    g_byte_array_append (b, (const guint8 *) &le, 4);
}

static void
put_f32 (GByteArray *b,
         gfloat      v)
{
    guint32 bits;

    memcpy (&bits, &v, 4);
    put_u32 (b, bits);
}

static void
put_link (GByteArray *b,
          const Link *l)
{
    guint k;

    for (k = 0; k < 3; k++)
        put_f32 (b, l->start[k]);
    for (k = 0; k < 3; k++)
        put_f32 (b, l->end[k]);
    put_f32 (b, l->cost);
    put_u32 (b, l->tag);
    put_u32 (b, l->poly_a);
    put_u32 (b, l->poly_b);
    put_u32 (b, (guint32) ((l->bidirectional ? 1u : 0u) | (l->enabled ? 2u : 0u) | (l->connected ? 4u : 0u)));
}

GBytes *
lrg_nav_poly_mesh_save (LrgNavPolyMesh *self)
{
    GByteArray *b;
    guint i;
    guint k;
    guint32 n_columns;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), NULL);

    b = g_byte_array_new ();
    g_byte_array_append (b, (const guint8 *) FORMAT_MAGIC, 8);
    put_u32 (b, FORMAT_VERSION);
    put_u32 (b, self->baked ? 1u : 0u);
    put_f32 (b, self->cell_size);
    put_f32 (b, self->cell_height);
    put_f32 (b, self->agent_radius);
    put_f32 (b, self->agent_height);
    put_f32 (b, self->max_climb);
    put_f32 (b, self->max_slope);
    put_f32 (b, self->max_drop);
    if (!self->baked)
        return g_byte_array_free_to_bytes (b);
    for (k = 0; k < 3; k++)
        put_f32 (b, self->origin[k]);
    put_f32 (b, self->bake_cs);
    put_f32 (b, self->bake_ch);
    put_u32 (b, (guint32) self->width);
    put_u32 (b, (guint32) self->depth);
    put_u32 (b, self->cells->len);
    put_u32 (b, self->polys->len);
    put_u32 (b, self->edges->len);
    put_u32 (b, self->links->len);
    put_u32 (b, self->drops->len);
    put_u32 (b, self->n_regions);
    n_columns = (guint32) self->width * (guint32) self->depth;
    /* column counts are stored instead of offsets (smaller to validate) */
    for (i = 0; i < n_columns; i++)
        put_u32 (b, self->col_first[i + 1] - self->col_first[i]);
    for (i = 0; i < self->cells->len; i++)
    {
        const Cell *c = &g_array_index (self->cells, Cell, i);

        put_f32 (b, c->y);
        put_u32 (b, (guint32) c->poly);
    }
    for (i = 0; i < self->polys->len; i++)
    {
        const Poly *p = &g_array_index (self->polys, Poly, i);

        put_u32 (b, (guint32) p->x0);
        put_u32 (b, (guint32) p->z0);
        put_u32 (b, (guint32) p->w);
        put_u32 (b, (guint32) p->h);
        put_u32 (b, p->tag);
        put_u32 (b, p->region);
        put_f32 (b, p->ymin);
        put_f32 (b, p->ymax);
        put_u32 (b, p->first_edge);
        put_u32 (b, p->n_edges);
        put_u32 (b, p->enabled ? 1u : 0u);
    }
    for (i = 0; i < self->edges->len; i++)
    {
        const Edge *e = &g_array_index (self->edges, Edge, i);

        put_u32 (b, (guint32) e->to);
        put_u32 (b, e->side);
        put_u32 (b, e->enabled ? 1u : 0u);
        for (k = 0; k < 3; k++)
            put_f32 (b, e->a[k]);
        for (k = 0; k < 3; k++)
            put_f32 (b, e->b[k]);
    }
    for (i = 0; i < self->links->len; i++)
        put_link (b, &g_array_index (self->links, Link, i));
    for (i = 0; i < self->drops->len; i++)
        put_link (b, &g_array_index (self->drops, Link, i));
    return g_byte_array_free_to_bytes (b);
}

typedef struct
{
    const guint8 *data;
    gsize         size;
    gsize         at;
    gboolean      ok;
} Reader;

static guint32
get_u32 (Reader *r)
{
    guint32 v;

    if (!r->ok || r->size - r->at < 4)
    {
        r->ok = FALSE;
        return 0;
    }
    memcpy (&v, r->data + r->at, 4);
    r->at += 4;
    return GUINT32_FROM_LE (v);
}

static gfloat
get_f32 (Reader *r)
{
    guint32 bits = get_u32 (r);
    gfloat v;

    memcpy (&v, &bits, 4);
    if (!isfinite (v))
        r->ok = FALSE;
    return v;
}

static void
get_link (Reader *r,
          Link   *l,
          guint   n_polys)
{
    guint k;
    guint32 flags;

    for (k = 0; k < 3; k++)
        l->start[k] = get_f32 (r);
    for (k = 0; k < 3; k++)
        l->end[k] = get_f32 (r);
    l->cost = get_f32 (r);
    l->tag = get_u32 (r);
    l->poly_a = get_u32 (r);
    l->poly_b = get_u32 (r);
    flags = get_u32 (r);
    l->bidirectional = (flags & 1u) != 0;
    l->enabled = (flags & 2u) != 0;
    l->connected = (flags & 4u) != 0;
    if (l->connected && (l->poly_a >= n_polys || l->poly_b >= n_polys))
        r->ok = FALSE;
}

gboolean
lrg_nav_poly_mesh_load (LrgNavPolyMesh  *self,
                        GBytes          *bytes,
                        GError         **error)
{
    Reader r;
    gfloat props[7];
    gfloat origin[3];
    gfloat cs;
    gfloat ch;
    guint32 width;
    guint32 depth;
    guint32 n_cells;
    guint32 n_polys;
    guint32 n_edges;
    guint32 n_links;
    guint32 n_drops;
    guint32 n_regions;
    guint32 n_columns;
    guint32 baked;
    g_autofree guint32 *col_first = NULL;
    g_autoptr(GArray) cells = NULL;
    g_autoptr(GArray) polys = NULL;
    g_autoptr(GArray) edges = NULL;
    g_autoptr(GArray) links = NULL;
    g_autoptr(GArray) drops = NULL;
    guint i;
    guint k;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (self), FALSE);
    g_return_val_if_fail (bytes != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    r.data = g_bytes_get_data (bytes, &r.size);
    r.at = 0;
    r.ok = r.size >= 8 && r.data != NULL && memcmp (r.data, FORMAT_MAGIC, 8) == 0;
    r.at = 8;
    if (r.ok && get_u32 (&r) != FORMAT_VERSION)
        r.ok = FALSE;
    baked = get_u32 (&r);
    for (k = 0; k < 7; k++)
        props[k] = get_f32 (&r);
    if (!r.ok || baked != 1u)
        goto invalid;
    for (k = 0; k < 3; k++)
        origin[k] = get_f32 (&r);
    cs = get_f32 (&r);
    ch = get_f32 (&r);
    width = get_u32 (&r);
    depth = get_u32 (&r);
    n_cells = get_u32 (&r);
    n_polys = get_u32 (&r);
    n_edges = get_u32 (&r);
    n_links = get_u32 (&r);
    n_drops = get_u32 (&r);
    n_regions = get_u32 (&r);
    if (!r.ok || cs <= 0.0f || ch <= 0.0f || width == 0 || depth == 0 ||
        (guint64) width * depth > LRG_NAV_POLY_MESH_MAX_COLUMNS || n_polys == 0 ||
        n_regions == 0 || n_regions > n_polys)
        goto invalid;
    n_columns = width * depth;
    /* every array must fit in what is left before allocating it */
    if ((guint64) (r.size - r.at) < (guint64) n_columns * 4 + (guint64) n_cells * 8 +
                                     (guint64) n_polys * 44 + (guint64) n_edges * 36 +
                                     (guint64) (n_links + (guint64) n_drops) * 44)
        goto invalid;
    col_first = g_new (guint32, n_columns + 1);
    col_first[0] = 0;
    for (i = 0; i < n_columns; i++)
    {
        guint32 count = get_u32 (&r);

        if ((guint64) col_first[i] + count > n_cells)
        {
            r.ok = FALSE;
            break;
        }
        col_first[i + 1] = col_first[i] + count;
    }
    if (!r.ok || col_first[n_columns] != n_cells)
        goto invalid;
    cells = g_array_sized_new (FALSE, FALSE, sizeof (Cell), n_cells);
    for (i = 0; i < n_cells && r.ok; i++)
    {
        Cell c;

        c.y = get_f32 (&r);
        c.poly = (gint32) get_u32 (&r);
        if (c.poly < 0 || (guint32) c.poly >= n_polys)
            r.ok = FALSE;
        g_array_append_val (cells, c);
    }
    polys = g_array_sized_new (FALSE, FALSE, sizeof (Poly), n_polys);
    for (i = 0; i < n_polys && r.ok; i++)
    {
        Poly p;

        p.x0 = (gint32) get_u32 (&r);
        p.z0 = (gint32) get_u32 (&r);
        p.w = (gint32) get_u32 (&r);
        p.h = (gint32) get_u32 (&r);
        p.tag = get_u32 (&r);
        p.region = get_u32 (&r);
        p.ymin = get_f32 (&r);
        p.ymax = get_f32 (&r);
        p.first_edge = get_u32 (&r);
        p.n_edges = get_u32 (&r);
        p.enabled = get_u32 (&r) != 0;
        if (p.x0 < 0 || p.z0 < 0 || p.w <= 0 || p.h <= 0 ||
            (guint64) p.x0 + (guint64) p.w > width || (guint64) p.z0 + (guint64) p.h > depth ||
            p.region == 0 || p.region > n_regions ||
            (guint64) p.first_edge + p.n_edges > n_edges)
            r.ok = FALSE;
        g_array_append_val (polys, p);
    }
    edges = g_array_sized_new (FALSE, FALSE, sizeof (Edge), n_edges);
    for (i = 0; i < n_edges && r.ok; i++)
    {
        Edge e;

        e.to = (gint32) get_u32 (&r);
        e.side = get_u32 (&r);
        e.enabled = get_u32 (&r) != 0;
        for (k = 0; k < 3; k++)
            e.a[k] = get_f32 (&r);
        for (k = 0; k < 3; k++)
            e.b[k] = get_f32 (&r);
        if (e.to < 0 || (guint32) e.to >= n_polys || e.side > 3)
            r.ok = FALSE;
        g_array_append_val (edges, e);
    }
    links = g_array_sized_new (FALSE, FALSE, sizeof (Link), n_links);
    for (i = 0; i < n_links && r.ok; i++)
    {
        Link l;

        get_link (&r, &l, n_polys);
        g_array_append_val (links, l);
    }
    drops = g_array_sized_new (FALSE, FALSE, sizeof (Link), n_drops);
    for (i = 0; i < n_drops && r.ok; i++)
    {
        Link l;

        get_link (&r, &l, n_polys);
        if (!l.connected)
            r.ok = FALSE;
        g_array_append_val (drops, l);
    }
    /* every cell inside its polygon's rectangle */
    for (i = 0; r.ok && i < n_columns; i++)
    {
        guint32 c;
        gint32 x = (gint32) (i % width);
        gint32 z = (gint32) (i / width);

        for (c = col_first[i]; c < col_first[i + 1]; c++)
        {
            const Poly *p = &g_array_index (polys, Poly, g_array_index (cells, Cell, c).poly);

            if (x < p->x0 || x >= p->x0 + p->w || z < p->z0 || z >= p->z0 + p->h)
            {
                r.ok = FALSE;
                break;
            }
        }
    }
    if (!r.ok || r.at != r.size)
        goto invalid;

    /* commit */
    free_bake (self);
    self->cell_size = props[0];
    self->cell_height = props[1];
    self->agent_radius = props[2];
    self->agent_height = props[3];
    self->max_climb = props[4];
    self->max_slope = props[5];
    self->max_drop = props[6];
    memcpy (self->origin, origin, sizeof (origin));
    self->bake_cs = cs;
    self->bake_ch = ch;
    self->width = (gint32) width;
    self->depth = (gint32) depth;
    self->col_first = g_steal_pointer (&col_first);
    self->cells = g_steal_pointer (&cells);
    self->polys = g_steal_pointer (&polys);
    self->edges = g_steal_pointer (&edges);
    g_array_unref (self->links);
    self->links = g_steal_pointer (&links);
    self->drops = g_steal_pointer (&drops);
    self->n_regions = n_regions;
    self->bake_seconds = 0.0;
    self->baked = TRUE;
    index_links (self);
    return TRUE;

invalid:
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "navigation mesh: not a complete navigation mesh of this format version");
    return FALSE;
}
