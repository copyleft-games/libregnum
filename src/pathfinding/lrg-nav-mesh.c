/* Triangle navigation mesh. SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-nav-mesh.h"
#include <gio/gio.h>
#include <math.h>
#include <string.h>

typedef struct
{
    gdouble x, y, z;
} Vec;
typedef struct
{
    Vec v[3], center;
    gint neighbor[3];
    gboolean enabled;
} Polygon;

struct _LrgNavMesh
{
    GObject parent_instance;
    GArray *polygons;
};
G_DEFINE_TYPE (LrgNavMesh, lrg_nav_mesh, G_TYPE_OBJECT)

static Vec
add (Vec a,
     Vec b)
{
    return (Vec){ a.x + b.x, a.y + b.y, a.z + b.z };
}
static Vec
sub (Vec a,
     Vec b)
{
    return (Vec){ a.x - b.x, a.y - b.y, a.z - b.z };
}
static Vec
scale (Vec a,
       gdouble s)
{
    return (Vec){ a.x * s, a.y * s, a.z * s };
}
static gdouble
dot (Vec a,
     Vec b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static gdouble
distance (Vec a,
          Vec b)
{
    Vec d = sub (a, b);

    return sqrt (dot (d, d));
}
static gboolean
equal (Vec a,
       Vec b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static Vec
read_vec (const gdouble *p)
{
    return (Vec){ p[0], p[1], p[2] };
}
static gboolean
valid_point (const gdouble *p)
{
    /* Keep cross/dot products well inside double's representable range. */
    return p != NULL && isfinite (p[0]) && isfinite (p[1]) && isfinite (p[2]) && fabs (p[0]) <= 1e12
           && fabs (p[1]) <= 1e12 && fabs (p[2]) <= 1e12;
}
static void
write_vec (gdouble *p,
           Vec v)
{
    p[0] = v.x;
    p[1] = v.y;
    p[2] = v.z;
}

static void
lrg_nav_mesh_finalize (GObject *object)
{
    g_array_unref (LRG_NAV_MESH (object)->polygons);
    G_OBJECT_CLASS (lrg_nav_mesh_parent_class)->finalize (object);
}
static void
lrg_nav_mesh_class_init (LrgNavMeshClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_nav_mesh_finalize;
}
static void
lrg_nav_mesh_init (LrgNavMesh *self)
{
    self->polygons = g_array_new (FALSE, FALSE, sizeof (Polygon));
}
LrgNavMesh *
lrg_nav_mesh_new (void)
{
    return g_object_new (LRG_TYPE_NAV_MESH, NULL);
}

typedef struct
{
    Vec a, b;
    guint polygon, side;
    gboolean paired;
} Edge;

static gint
compare_vec (Vec a,
             Vec b)
{
    if (a.x != b.x)
        return a.x < b.x ? -1 : 1;
    if (a.y != b.y)
        return a.y < b.y ? -1 : 1;
    return (a.z > b.z) - (a.z < b.z);
}

static guint
hash_coordinate (gdouble value)
{
    guint64 bits;

    if (value == 0)
        value = 0; /* Canonicalize negative zero. */
    memcpy (&bits, &value, sizeof (bits));
    return (guint)(bits ^ (bits >> 32));
}

static guint
edge_hash (gconstpointer data)
{
    const Edge *e = data;
    guint h = hash_coordinate (e->a.x);

    h = h * 31 + hash_coordinate (e->a.y);
    h = h * 31 + hash_coordinate (e->a.z);
    h = h * 31 + hash_coordinate (e->b.x);
    h = h * 31 + hash_coordinate (e->b.y);
    return h * 31 + hash_coordinate (e->b.z);
}

static gboolean
edge_equal (gconstpointer a,
            gconstpointer b)
{
    const Edge *left = a;
    const Edge *right = b;

    return equal (left->a, right->a) && equal (left->b, right->b);
}

gboolean
lrg_nav_mesh_bake (LrgNavMesh *self,
                   const gdouble *vertices,
                   guint n_coordinates,
                   const guint *indices,
                   guint n_indices,
                   gdouble max_slope,
                   GError **error)
{
    g_autoptr (GArray) polygons = NULL;
    g_autoptr (GHashTable) edges = NULL;
    guint i, j;
    gdouble cosine;

    g_return_val_if_fail (LRG_IS_NAV_MESH (self), FALSE);
    if (vertices == NULL || indices == NULL || n_coordinates == 0 || n_indices == 0
        || n_coordinates % 3 != 0 || n_indices % 3 != 0 || !isfinite (max_slope) || max_slope < 0
        || max_slope >= 90)
        goto invalid;
    for (i = 0; i < n_coordinates; i += 3)
        if (!valid_point (vertices + i))
            goto invalid;
    for (i = 0; i < n_indices; i++)
        if (indices[i] >= n_coordinates / 3)
            goto invalid;
    polygons = g_array_new (FALSE, FALSE, sizeof (Polygon));
    cosine = cos (max_slope * G_PI / 180.0);
    for (i = 0; i < n_indices; i += 3)
    {
        Polygon p;
        Vec u, v, normal;
        gdouble length;

        for (j = 0; j < 3; j++)
        {
            p.v[j] = read_vec (vertices + indices[i + j] * 3);
            p.neighbor[j] = -1;
        }
        u = sub (p.v[1], p.v[0]);
        v = sub (p.v[2], p.v[0]);
        normal = (Vec){ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        length = sqrt (dot (normal, normal));
        if (length < 1e-12 || fabs (normal.y) / length < cosine)
            continue;
        p.center = scale (add (add (p.v[0], p.v[1]), p.v[2]), 1.0 / 3.0);
        p.enabled = TRUE;
        g_array_append_val (polygons, p);
    }
    if (polygons->len == 0 || polygons->len > G_MAXINT)
        goto invalid;
    edges = g_hash_table_new_full (edge_hash, edge_equal, g_free, NULL);
    for (i = 0; i < polygons->len; i++)
    {
        Polygon *p = &g_array_index (polygons, Polygon, i);

        for (j = 0; j < 3; j++)
        {
            Edge key;
            Edge *existing;
            Vec a = p->v[j];
            Vec b = p->v[(j + 1) % 3];

            key.a = compare_vec (a, b) < 0 ? a : b;
            key.b = compare_vec (a, b) < 0 ? b : a;
            key.polygon = i;
            key.side = j;
            key.paired = FALSE;
            existing = g_hash_table_lookup (edges, &key);
            if (existing != NULL)
            {
                Polygon *q = &g_array_index (polygons, Polygon, existing->polygon);
                guint k;

                if (existing->paired)
                    goto invalid;
                /* Multiple common edges imply duplicate/overlapping triangles. */
                for (k = 0; k < 3; k++)
                    if (p->neighbor[k] == (gint)existing->polygon)
                        goto invalid;
                existing->paired = TRUE;
                p->neighbor[j] = (gint)existing->polygon;
                q->neighbor[existing->side] = (gint)i;
            }
            else
            {
                Edge *stored = g_new (Edge, 1);

                *stored = key;
                g_hash_table_add (edges, stored);
            }
        }
    }
    g_array_unref (self->polygons);
    self->polygons = g_steal_pointer (&polygons);
    return TRUE;
invalid:
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Invalid geometry, non-manifold edges, or no walkable triangles");
    return FALSE;
}

guint
lrg_nav_mesh_get_polygon_count (LrgNavMesh *self)
{
    g_return_val_if_fail (LRG_IS_NAV_MESH (self), 0);
    return self->polygons->len;
}

gboolean
lrg_nav_mesh_set_enabled (LrgNavMesh *self,
                          guint polygon,
                          gboolean enabled)
{
    g_return_val_if_fail (LRG_IS_NAV_MESH (self), FALSE);
    if (polygon >= self->polygons->len)
        return FALSE;
    g_array_index (self->polygons, Polygon, polygon).enabled = enabled;
    return TRUE;
}

static Vec
closest (Polygon *p,
         Vec point)
{
    Vec a = p->v[0], b = p->v[1], c = p->v[2];
    Vec ab = sub (b, a), ac = sub (c, a), ap = sub (point, a);
    Vec bp, cp;
    gdouble d1, d2, d3, d4, d5, d6, va, vb, vc, inverse;

    /* Classify the seven Voronoi regions of a nondegenerate triangle. */
    d1 = dot (ab, ap);
    d2 = dot (ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    bp = sub (point, b);
    d3 = dot (ab, bp);
    d4 = dot (ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return add (a, scale (ab, d1 / (d1 - d3)));
    cp = sub (point, c);
    d5 = dot (ab, cp);
    d6 = dot (ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return add (a, scale (ac, d2 / (d2 - d6)));
    va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return add (b, scale (sub (c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    inverse = 1.0 / (va + vb + vc);
    return add (a, add (scale (ab, vb * inverse), scale (ac, vc * inverse)));
}

gboolean
lrg_nav_mesh_project (LrgNavMesh *self,
                      const gdouble *point,
                      gdouble max_distance,
                      gdouble *projected,
                      guint *polygon)
{
    gdouble best;
    Vec result = { 0, 0, 0 };
    guint selected = G_MAXUINT;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_MESH (self), FALSE);
    g_return_val_if_fail (projected != NULL, FALSE);
    if (!valid_point (point) || !isfinite (max_distance) || max_distance < 0)
        return FALSE;
    best = max_distance;
    for (i = 0; i < self->polygons->len; i++)
    {
        Polygon *p = &g_array_index (self->polygons, Polygon, i);
        Vec candidate;
        gdouble d;

        if (!p->enabled)
            continue;
        candidate = closest (p, read_vec (point));
        d = distance (candidate, read_vec (point));
        if (d <= best && (selected == G_MAXUINT || d < best))
        {
            best = d;
            selected = i;
            result = candidate;
        }
    }
    if (selected == G_MAXUINT)
        return FALSE;
    write_vec (projected, result);
    if (polygon != NULL)
        *polygon = selected;
    return TRUE;
}

typedef struct
{
    guint polygon;
    gdouble estimate;
} SearchNode;

static gint
compare_search (gconstpointer a,
                gconstpointer b,
                gpointer data)
{
    const SearchNode *left = a;
    const SearchNode *right = b;

    if (left->estimate != right->estimate)
        return left->estimate < right->estimate ? -1 : 1;
    return (left->polygon > right->polygon) - (left->polygon < right->polygon);
}

static void
queue_polygon (GSequence *open,
               GSequenceIter **positions,
               guint polygon,
               gdouble estimate)
{
    SearchNode *node;

    if (positions[polygon] != NULL)
    {
        node = g_sequence_get (positions[polygon]);
        node->estimate = estimate;
        g_sequence_sort_changed (positions[polygon], compare_search, NULL);
    }
    else
    {
        node = g_new (SearchNode, 1);
        node->polygon = polygon;
        node->estimate = estimate;
        positions[polygon] = g_sequence_insert_sorted (open, node, compare_search, NULL);
    }
}

gdouble *
lrg_nav_mesh_find_path (LrgNavMesh *self,
                        const gdouble *start,
                        const gdouble *goal,
                        gdouble max_distance,
                        guint *n_coordinates,
                        GError **error)
{
    g_autofree gdouble *cost = NULL;
    g_autofree gint *previous = NULL;
    g_autofree gboolean *closed = NULL;
    g_autofree GSequenceIter **positions = NULL;
    g_autoptr (GSequence) open = NULL;
    g_autoptr (GArray) reverse = NULL;
    gdouble from[3], to[3];
    gdouble *result;
    guint src, dst, n, i, edge, current;

    g_return_val_if_fail (LRG_IS_NAV_MESH (self), NULL);
    g_return_val_if_fail (n_coordinates != NULL, NULL);
    *n_coordinates = 0;
    if (!valid_point (start) || !valid_point (goal) || !isfinite (max_distance) || max_distance < 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Invalid path endpoints or projection distance");
        return NULL;
    }
    if (!lrg_nav_mesh_project (self, start, max_distance, from, &src)
        || !lrg_nav_mesh_project (self, goal, max_distance, to, &dst))
        goto no_path;
    n = self->polygons->len;
    cost = g_new (gdouble, n);
    previous = g_new (gint, n);
    closed = g_new0 (gboolean, n);
    for (i = 0; i < n; i++)
    {
        cost[i] = G_MAXDOUBLE;
        previous[i] = -1;
    }
    cost[src] = 0;
    positions = g_new0 (GSequenceIter *, n);
    open = g_sequence_new (g_free);
    queue_polygon (open, positions, src, 0);
    for (;;)
    {
        Vec target = g_array_index (self->polygons, Polygon, dst).center;
        Polygon *p;
        GSequenceIter *first;

        if (g_sequence_is_empty (open))
            goto no_path;
        first = g_sequence_get_begin_iter (open);
        current = ((SearchNode *)g_sequence_get (first))->polygon;
        positions[current] = NULL;
        g_sequence_remove (first);
        if (current == dst)
            break;
        closed[current] = TRUE;
        p = &g_array_index (self->polygons, Polygon, current);
        for (edge = 0; edge < 3; edge++)
        {
            gint next = p->neighbor[edge];
            Polygon *q;
            gdouble candidate;

            if (next < 0 || closed[next])
                continue;
            q = &g_array_index (self->polygons, Polygon, next);
            if (!q->enabled)
                continue;
            candidate = cost[current] + distance (p->center, q->center);
            if (candidate < cost[next])
            {
                cost[next] = candidate;
                previous[next] = (gint)current;
                queue_polygon (open, positions, (guint)next,
                               candidate + distance (q->center, target));
            }
        }
    }
    reverse = g_array_new (FALSE, FALSE, sizeof (guint));
    for (current = dst;; current = (guint)previous[current])
    {
        g_array_append_val (reverse, current);
        if (current == src)
            break;
    }
    if (reverse->len > G_MAXUINT / 3 - 1)
        goto no_path;
    *n_coordinates = (reverse->len + 1) * 3;
    result = g_new (gdouble, *n_coordinates);
    write_vec (result, read_vec (from));
    for (i = 1; i < reverse->len; i++)
    {
        guint a = g_array_index (reverse, guint, reverse->len - i);
        guint b = g_array_index (reverse, guint, reverse->len - i - 1);
        Polygon *p = &g_array_index (self->polygons, Polygon, a);

        for (edge = 0; edge < 3; edge++)
            if (p->neighbor[edge] == (gint)b)
            {
                write_vec (result + i * 3, scale (add (p->v[edge], p->v[(edge + 1) % 3]), 0.5));
                break;
            }
    }
    write_vec (result + *n_coordinates - 3, read_vec (to));
    return result;
no_path:
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No connected walkable route");
    return NULL;
}
