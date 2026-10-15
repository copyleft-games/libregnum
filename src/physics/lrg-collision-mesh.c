/* lrg-collision-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Static triangle soup in a binned-SAH bounding volume hierarchy, plus
 * transformed instances of other meshes.
 *
 * Storage: the own triangles are kept as given (nine floats each) with
 * their tags; the hierarchy references them through a permutation array
 * so triangle indices reported in hits are the caller's indices.
 * Zero-area triangles never enter the hierarchy.
 *
 * Queries:
 *  - ray casts walk the hierarchy front to back with a slab test and a
 *    double-sided Moller-Trumbore test in double precision;
 *  - sweeps and overlaps gather candidate triangles with a box query and
 *    run conservative advancement on the segment-to-triangle distance.
 *    For a translating convex shape against a convex triangle that
 *    distance is a convex function of time, so a Newton step from the
 *    start never overshoots the first contact: the sweep cannot tunnel
 *    and converges in a handful of iterations.
 *
 * Instances are tested after the own triangles in ascending handle order
 * by transforming the query into the shape's model space (rays) or the
 * shape's triangles into world space (sweeps). Everything is
 * deterministic: no randomness, fixed traversal orders, first-found wins
 * ties.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-collision-mesh.h"
#include "physics/lrg-collision-mesh-private.h"

/* Most triangles kept in one hierarchy leaf. */
#define BVH_LEAF_SIZE        (4u)
/* SAH bins per split. */
#define BVH_BINS             (16u)
/* Traversal stack entries kept on the C stack before spilling to the heap. */
#define BVH_STACK_LOCAL      (128u)
/* Gap (world units) at which conservative advancement reports contact. */
#define SWEEP_CONTACT_TOL    (1e-5)
/* Relative approach speed below which a contact does not block. */
#define SWEEP_APPROACH_EPS   (1e-6)
/* Conservative advancement iterations per triangle. */
#define SWEEP_MAX_ITERATIONS (48)
/* Distances below this are treated as touching (normal from the face). */
#define DIST_EPS             (1e-9)

/* ------------------------------------------------------------------------ */
/* Small vector helpers (double precision)                                  */
/* ------------------------------------------------------------------------ */

static void
v3_sub (const gdouble *a,
        const gdouble *b,
        gdouble       *out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void
v3_add_scaled (const gdouble *a,
               const gdouble *b,
               gdouble        s,
               gdouble       *out)
{
    out[0] = a[0] + b[0] * s;
    out[1] = a[1] + b[1] * s;
    out[2] = a[2] + b[2] * s;
}

static gdouble
v3_dot (const gdouble *a,
        const gdouble *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
v3_cross (const gdouble *a,
          const gdouble *b,
          gdouble       *out)
{
    gdouble x;
    gdouble y;
    gdouble z;

    x = a[1] * b[2] - a[2] * b[1];
    y = a[2] * b[0] - a[0] * b[2];
    z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static gdouble
v3_length (const gdouble *a)
{
    return sqrt (v3_dot (a, a));
}

static gboolean
v3f_finite (const gfloat *a)
{
    return isfinite (a[0]) && isfinite (a[1]) && isfinite (a[2]);
}

/* Transforms a point by a column-major 4x4 affine matrix. */
static void
m4_point (const gdouble *m,
          const gdouble *p,
          gdouble       *out)
{
    gdouble x;
    gdouble y;
    gdouble z;

    x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* Transforms a direction (linear part only). */
static void
m4_vector (const gdouble *m,
           const gdouble *v,
           gdouble       *out)
{
    gdouble x;
    gdouble y;
    gdouble z;

    x = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
    y = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
    z = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* out = a * b (b applied first); may not alias. */
static void
m4_multiply (const gdouble *a,
             const gdouble *b,
             gdouble       *out)
{
    guint col;
    guint row;
    guint k;

    for (col = 0; col < 4; col++)
    {
        for (row = 0; row < 4; row++)
        {
            gdouble sum;

            sum = 0.0;
            for (k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    }
}

/*
 * Inverts an affine matrix (bottom row 0 0 0 1 assumed). Returns FALSE
 * for a singular or non-finite matrix.
 */
static gboolean
m4_affine_inverse (const gdouble *m,
                   gdouble       *out)
{
    gdouble a00, a01, a02, a10, a11, a12, a20, a21, a22;
    gdouble c00, c01, c02, c10, c11, c12, c20, c21, c22;
    gdouble det;
    gdouble inv_det;
    guint i;

    for (i = 0; i < 16; i++)
    {
        if (!isfinite (m[i]))
            return FALSE;
    }

    /* the linear part, a_rc = m[c * 4 + r] */
    a00 = m[0]; a10 = m[1]; a20 = m[2];
    a01 = m[4]; a11 = m[5]; a21 = m[6];
    a02 = m[8]; a12 = m[9]; a22 = m[10];

    /* cofactors c_rc */
    c00 = a11 * a22 - a12 * a21;
    c01 = -(a10 * a22 - a12 * a20);
    c02 = a10 * a21 - a11 * a20;
    c10 = -(a01 * a22 - a02 * a21);
    c11 = a00 * a22 - a02 * a20;
    c12 = -(a00 * a21 - a01 * a20);
    c20 = a01 * a12 - a02 * a11;
    c21 = -(a00 * a12 - a02 * a10);
    c22 = a00 * a11 - a01 * a10;

    det = a00 * c00 + a01 * c01 + a02 * c02;
    if (!isfinite (det) || fabs (det) < 1e-12)
        return FALSE;
    inv_det = 1.0 / det;

    /* inverse = adjugate / det, adjugate_rc = c_cr; out[c * 4 + r] */
    out[0] = c00 * inv_det;
    out[1] = c01 * inv_det;
    out[2] = c02 * inv_det;
    out[4] = c10 * inv_det;
    out[5] = c11 * inv_det;
    out[6] = c12 * inv_det;
    out[8] = c20 * inv_det;
    out[9] = c21 * inv_det;
    out[10] = c22 * inv_det;
    out[3] = 0.0;
    out[7] = 0.0;
    out[11] = 0.0;
    out[15] = 1.0;

    /* translation: -L^-1 * t */
    out[12] = -(out[0] * m[12] + out[4] * m[13] + out[8] * m[14]);
    out[13] = -(out[1] * m[12] + out[5] * m[13] + out[9] * m[14]);
    out[14] = -(out[2] * m[12] + out[6] * m[13] + out[10] * m[14]);
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Geometry primitives (shared through the private header)                  */
/* ------------------------------------------------------------------------ */

gboolean
lrg_collision_triangle_normal (const gdouble *tri,
                               gdouble       *out)
{
    gdouble e1[3];
    gdouble e2[3];
    gdouble len;

    v3_sub (tri + 3, tri, e1);
    v3_sub (tri + 6, tri, e2);
    v3_cross (e1, e2, out);
    len = v3_length (out);
    if (!(len > 1e-30))
    {
        out[0] = 0.0;
        out[1] = 1.0;
        out[2] = 0.0;
        return FALSE;
    }
    out[0] /= len;
    out[1] /= len;
    out[2] /= len;
    return TRUE;
}

/* Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5). */
static void
closest_point_triangle (const gdouble *p,
                        const gdouble *tri,
                        gdouble       *out)
{
    const gdouble *a;
    const gdouble *b;
    const gdouble *c;
    gdouble ab[3];
    gdouble ac[3];
    gdouble ap[3];
    gdouble bp[3];
    gdouble cp[3];
    gdouble d1;
    gdouble d2;
    gdouble d3;
    gdouble d4;
    gdouble d5;
    gdouble d6;
    gdouble va;
    gdouble vb;
    gdouble vc;
    gdouble v;
    gdouble w;
    gdouble denom;
    gdouble bc[3];

    a = tri;
    b = tri + 3;
    c = tri + 6;
    v3_sub (b, a, ab);
    v3_sub (c, a, ac);
    v3_sub (p, a, ap);

    /* vertex region a */
    d1 = v3_dot (ab, ap);
    d2 = v3_dot (ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
    {
        memcpy (out, a, sizeof (gdouble) * 3);
        return;
    }

    /* vertex region b */
    v3_sub (p, b, bp);
    d3 = v3_dot (ab, bp);
    d4 = v3_dot (ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
    {
        memcpy (out, b, sizeof (gdouble) * 3);
        return;
    }

    /* edge region ab */
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        v = d1 / (d1 - d3);
        v3_add_scaled (a, ab, v, out);
        return;
    }

    /* vertex region c */
    v3_sub (p, c, cp);
    d5 = v3_dot (ab, cp);
    d6 = v3_dot (ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
    {
        memcpy (out, c, sizeof (gdouble) * 3);
        return;
    }

    /* edge region ac */
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        w = d2 / (d2 - d6);
        v3_add_scaled (a, ac, w, out);
        return;
    }

    /* edge region bc */
    va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
    {
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        v3_sub (c, b, bc);
        v3_add_scaled (b, bc, w, out);
        return;
    }

    /* face region */
    denom = va + vb + vc;
    if (!(fabs (denom) > 0.0))
    {
        memcpy (out, a, sizeof (gdouble) * 3);
        return;
    }
    denom = 1.0 / denom;
    v = vb * denom;
    w = vc * denom;
    out[0] = a[0] + ab[0] * v + ac[0] * w;
    out[1] = a[1] + ab[1] * v + ac[1] * w;
    out[2] = a[2] + ab[2] * v + ac[2] * w;
}

/*
 * Closest points between segments p1-q1 and p2-q2 (Ericson 5.1.9).
 * Returns the squared distance.
 */
static gdouble
closest_segment_segment (const gdouble *p1,
                         const gdouble *q1,
                         const gdouble *p2,
                         const gdouble *q2,
                         gdouble       *c1,
                         gdouble       *c2)
{
    gdouble d1[3];
    gdouble d2[3];
    gdouble r[3];
    gdouble diff[3];
    gdouble a;
    gdouble e;
    gdouble f;
    gdouble s;
    gdouble t;

    v3_sub (q1, p1, d1);
    v3_sub (q2, p2, d2);
    v3_sub (p1, p2, r);
    a = v3_dot (d1, d1);
    e = v3_dot (d2, d2);
    f = v3_dot (d2, r);

    if (a <= DIST_EPS * DIST_EPS && e <= DIST_EPS * DIST_EPS)
    {
        s = 0.0;
        t = 0.0;
    }
    else if (a <= DIST_EPS * DIST_EPS)
    {
        s = 0.0;
        t = CLAMP (f / e, 0.0, 1.0);
    }
    else
    {
        gdouble c;

        c = v3_dot (d1, r);
        if (e <= DIST_EPS * DIST_EPS)
        {
            t = 0.0;
            s = CLAMP (-c / a, 0.0, 1.0);
        }
        else
        {
            gdouble b;
            gdouble denom;

            b = v3_dot (d1, d2);
            denom = a * e - b * b;
            if (denom > 0.0)
                s = CLAMP ((b * f - c * e) / denom, 0.0, 1.0);
            else
                s = 0.0;
            t = (b * s + f) / e;
            if (t < 0.0)
            {
                t = 0.0;
                s = CLAMP (-c / a, 0.0, 1.0);
            }
            else if (t > 1.0)
            {
                t = 1.0;
                s = CLAMP ((b - c) / a, 0.0, 1.0);
            }
        }
    }

    v3_add_scaled (p1, d1, s, c1);
    v3_add_scaled (p2, d2, t, c2);
    v3_sub (c1, c2, diff);
    return v3_dot (diff, diff);
}

/*
 * Segment p-q against triangle: TRUE with the crossing point when the
 * segment passes through the triangle's interior or boundary.
 */
static gboolean
segment_crosses_triangle (const gdouble *p,
                          const gdouble *q,
                          const gdouble *tri,
                          gdouble       *out)
{
    gdouble e1[3];
    gdouble e2[3];
    gdouble d[3];
    gdouble h[3];
    gdouble s[3];
    gdouble qv[3];
    gdouble det;
    gdouble inv;
    gdouble u;
    gdouble v;
    gdouble t;
    gdouble scale;

    v3_sub (tri + 3, tri, e1);
    v3_sub (tri + 6, tri, e2);
    v3_sub (q, p, d);
    v3_cross (d, e2, h);
    det = v3_dot (e1, h);
    scale = v3_length (e1) * v3_length (e2) * v3_length (d);
    if (!(fabs (det) > 1e-12 * scale))
        return FALSE;
    inv = 1.0 / det;
    v3_sub (p, tri, s);
    u = v3_dot (s, h) * inv;
    if (u < 0.0 || u > 1.0)
        return FALSE;
    v3_cross (s, e1, qv);
    v = v3_dot (d, qv) * inv;
    if (v < 0.0 || u + v > 1.0)
        return FALSE;
    t = v3_dot (e2, qv) * inv;
    if (t < 0.0 || t > 1.0)
        return FALSE;
    v3_add_scaled (p, d, t, out);
    return TRUE;
}

gdouble
lrg_collision_segment_triangle (const gdouble *a,
                                const gdouble *b,
                                const gdouble *tri,
                                gdouble       *on_segment,
                                gdouble       *on_triangle)
{
    gdouble best;
    gdouble cand_s[3];
    gdouble cand_t[3];
    gdouble diff[3];
    gdouble d2;
    guint edge;

    /* a segment through the triangle touches it */
    if (segment_crosses_triangle (a, b, tri, cand_s))
    {
        memcpy (on_segment, cand_s, sizeof (cand_s));
        memcpy (on_triangle, cand_s, sizeof (cand_s));
        return 0.0;
    }

    /* otherwise the closest pair involves an endpoint or a triangle edge */
    closest_point_triangle (a, tri, cand_t);
    v3_sub (a, cand_t, diff);
    best = v3_dot (diff, diff);
    memcpy (on_segment, a, sizeof (gdouble) * 3);
    memcpy (on_triangle, cand_t, sizeof (cand_t));

    closest_point_triangle (b, tri, cand_t);
    v3_sub (b, cand_t, diff);
    d2 = v3_dot (diff, diff);
    if (d2 < best)
    {
        best = d2;
        memcpy (on_segment, b, sizeof (gdouble) * 3);
        memcpy (on_triangle, cand_t, sizeof (cand_t));
    }

    for (edge = 0; edge < 3; edge++)
    {
        const gdouble *e0;
        const gdouble *e1;

        e0 = tri + edge * 3;
        e1 = tri + ((edge + 1) % 3) * 3;
        d2 = closest_segment_segment (a, b, e0, e1, cand_s, cand_t);
        if (d2 < best)
        {
            best = d2;
            memcpy (on_segment, cand_s, sizeof (cand_s));
            memcpy (on_triangle, cand_t, sizeof (cand_t));
        }
    }
    return sqrt (best);
}

/*
 * Time of first contact of the swept-sphere volume of a-b (radius r)
 * moving by m against one triangle, by conservative advancement.
 * Returns TRUE with the fraction, contact normal (towards the shape)
 * and contact point on the triangle.
 */
static gboolean
sweep_triangle (const gdouble *a,
                const gdouble *b,
                gdouble        r,
                const gdouble *m,
                const gdouble *tri,
                gdouble       *out_t,
                gdouble       *out_n,
                gdouble       *out_p)
{
    gdouble mlen;
    gdouble t;
    guint iter;
    gdouble pa[3];
    gdouble pb[3];
    gdouble cs[3];
    gdouble ct[3];
    gdouble n[3];

    mlen = v3_length (m);
    if (!(mlen > 1e-12))
        return FALSE;

    t = 0.0;
    for (iter = 0; iter < SWEEP_MAX_ITERATIONS; iter++)
    {
        gdouble d;
        gdouble gap;
        gdouble rate;

        v3_add_scaled (a, m, t, pa);
        v3_add_scaled (b, m, t, pb);
        d = lrg_collision_segment_triangle (pa, pb, tri, cs, ct);

        if (d > DIST_EPS)
        {
            v3_sub (cs, ct, n);
            n[0] /= d;
            n[1] /= d;
            n[2] /= d;
        }
        else
        {
            /* touching or crossing: the face normal against the motion */
            lrg_collision_triangle_normal (tri, n);
            if (v3_dot (n, m) > 0.0)
            {
                n[0] = -n[0];
                n[1] = -n[1];
                n[2] = -n[2];
            }
        }

        rate = -v3_dot (n, m);
        gap = d - r;

        if (gap <= SWEEP_CONTACT_TOL)
        {
            /* in contact: blocks only motion that goes deeper */
            if (rate <= SWEEP_APPROACH_EPS * mlen)
                return FALSE;
            *out_t = t;
            memcpy (out_n, n, sizeof (n));
            memcpy (out_p, ct, sizeof (ct));
            return TRUE;
        }

        /* the distance is convex in t: no approach now means none later */
        if (rate <= 1e-12 * mlen)
            return FALSE;

        t += gap / rate;
        if (t > 1.0)
            return FALSE;
    }

    /* not converged (a grazing approach): stop conservatively here */
    *out_t = t;
    memcpy (out_n, n, sizeof (n));
    memcpy (out_p, ct, sizeof (ct));
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Mesh data                                                                */
/* ------------------------------------------------------------------------ */

/*
 * BvhNode:
 *
 * Interior nodes have count == 0 and their children at first and
 * first + 1. Leaves list order[first .. first + count).
 */
typedef struct
{
    gfloat bmin[3];
    gfloat bmax[3];
    guint  first;
    guint  count;
} BvhNode;

typedef struct
{
    LrgCollisionMesh *shape;     /* owned reference, NULL when free */
    gdouble           m[16];     /* model to world */
    gdouble           inv[16];   /* world to model */
    guint64           id;
} Instance;

struct _LrgCollisionMesh
{
    GObject   parent_instance;

    gfloat   *vertices;      /* 9 per triangle, caller order */
    guint32  *tags;          /* 1 per triangle */
    guint     n_triangles;

    BvhNode  *nodes;
    guint     n_nodes;
    guint    *order;         /* triangle indices referenced by leaves */
    guint     n_order;
    guint     max_depth;

    GArray   *instances;     /* Instance; free slots have shape == NULL */
    guint     n_live;
};

G_DEFINE_TYPE (LrgCollisionMesh, lrg_collision_mesh, G_TYPE_OBJECT)

static void
lrg_collision_mesh_finalize (GObject *object)
{
    LrgCollisionMesh *self;
    guint i;

    self = LRG_COLLISION_MESH (object);
    for (i = 0; i < self->instances->len; i++)
    {
        Instance *inst;

        inst = &g_array_index (self->instances, Instance, i);
        g_clear_object (&inst->shape);
    }
    g_array_unref (self->instances);
    g_free (self->vertices);
    g_free (self->tags);
    g_free (self->nodes);
    g_free (self->order);

    G_OBJECT_CLASS (lrg_collision_mesh_parent_class)->finalize (object);
}

static void
lrg_collision_mesh_class_init (LrgCollisionMeshClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_collision_mesh_finalize;
}

static void
lrg_collision_mesh_init (LrgCollisionMesh *self)
{
    self->instances = g_array_new (FALSE, TRUE, sizeof (Instance));
}

LrgCollisionMesh *
lrg_collision_mesh_new (void)
{
    return (LrgCollisionMesh *) g_object_new (LRG_TYPE_COLLISION_MESH, NULL);
}

/* Loads triangle i as doubles. */
static void
triangle_load (LrgCollisionMesh *self,
               guint             i,
               gdouble          *out)
{
    const gfloat *v;
    guint k;

    v = self->vertices + (gsize) i * 9;
    for (k = 0; k < 9; k++)
        out[k] = (gdouble) v[k];
}

/* ------------------------------------------------------------------------ */
/* Hierarchy build                                                          */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gfloat bmin[3];
    gfloat bmax[3];
    guint  count;
} Bin;

static void
box_reset (gfloat *bmin,
           gfloat *bmax)
{
    bmin[0] = bmin[1] = bmin[2] = G_MAXFLOAT;
    bmax[0] = bmax[1] = bmax[2] = -G_MAXFLOAT;
}

static void
box_grow_box (gfloat       *bmin,
              gfloat       *bmax,
              const gfloat *omin,
              const gfloat *omax)
{
    guint k;

    for (k = 0; k < 3; k++)
    {
        bmin[k] = MIN (bmin[k], omin[k]);
        bmax[k] = MAX (bmax[k], omax[k]);
    }
}

static gdouble
box_area (const gfloat *bmin,
          const gfloat *bmax)
{
    gdouble dx;
    gdouble dy;
    gdouble dz;

    if (bmin[0] > bmax[0])
        return 0.0;
    dx = (gdouble) bmax[0] - bmin[0];
    dy = (gdouble) bmax[1] - bmin[1];
    dz = (gdouble) bmax[2] - bmin[2];
    return 2.0 * (dx * dy + dy * dz + dz * dx);
}

typedef struct
{
    gfloat *tri_min;     /* 3 per triangle */
    gfloat *tri_max;
    gfloat *centroid;
} BuildData;

static void
node_bounds (LrgCollisionMesh *self,
             const BuildData  *bd,
             BvhNode          *node)
{
    guint i;

    box_reset (node->bmin, node->bmax);
    for (i = node->first; i < node->first + node->count; i++)
    {
        guint t;

        t = self->order[i];
        box_grow_box (node->bmin, node->bmax, bd->tri_min + t * 3, bd->tri_max + t * 3);
    }
}

/*
 * Splits node index ni if worthwhile; returns TRUE with the two children
 * appended when it did.
 */
static gboolean
node_split (LrgCollisionMesh *self,
            const BuildData  *bd,
            guint             ni)
{
    BvhNode *node;
    gfloat cmin[3];
    gfloat cmax[3];
    guint axis;
    guint i;
    gfloat extent;
    Bin bins[BVH_BINS];
    gdouble left_area[BVH_BINS];
    gfloat left_min[BVH_BINS][3];
    gfloat left_max[BVH_BINS][3];
    gfloat best_min[2][3];
    gfloat best_max[2][3];
    gboolean have_bounds;
    guint left_count[BVH_BINS];
    gfloat accmin[3];
    gfloat accmax[3];
    guint acc;
    gdouble best_cost;
    guint best_split;
    guint lo;
    guint hi;
    guint first;
    guint count;
    guint left_n;

    node = &self->nodes[ni];
    first = node->first;
    count = node->count;
    if (count <= BVH_LEAF_SIZE)
        return FALSE;

    /* centroid bounds pick the axis */
    box_reset (cmin, cmax);
    for (i = first; i < first + count; i++)
    {
        const gfloat *c;

        c = bd->centroid + self->order[i] * 3;
        box_grow_box (cmin, cmax, c, c);
    }
    axis = 0;
    if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis])
        axis = 1;
    if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis])
        axis = 2;
    extent = cmax[axis] - cmin[axis];

    best_split = 0;
    have_bounds = FALSE;
    if (extent > 0.0f)
    {
        /* bin the centroids */
        for (i = 0; i < BVH_BINS; i++)
        {
            box_reset (bins[i].bmin, bins[i].bmax);
            bins[i].count = 0;
        }
        for (i = first; i < first + count; i++)
        {
            guint t;
            guint b;

            t = self->order[i];
            b = (guint) (((bd->centroid[t * 3 + axis] - cmin[axis]) / extent) * (gfloat) BVH_BINS);
            if (b >= BVH_BINS)
                b = BVH_BINS - 1;
            bins[b].count++;
            box_grow_box (bins[b].bmin, bins[b].bmax, bd->tri_min + t * 3, bd->tri_max + t * 3);
        }

        /* left prefix areas, then sweep from the right for the SAH cost */
        box_reset (accmin, accmax);
        acc = 0;
        for (i = 0; i < BVH_BINS - 1; i++)
        {
            acc += bins[i].count;
            box_grow_box (accmin, accmax, bins[i].bmin, bins[i].bmax);
            left_area[i] = box_area (accmin, accmax);
            left_count[i] = acc;
            memcpy (left_min[i], accmin, sizeof (accmin));
            memcpy (left_max[i], accmax, sizeof (accmax));
        }
        box_reset (accmin, accmax);
        acc = 0;
        best_cost = G_MAXDOUBLE;
        for (i = BVH_BINS - 1; i > 0; i--)
        {
            gdouble cost;

            acc += bins[i].count;
            box_grow_box (accmin, accmax, bins[i].bmin, bins[i].bmax);
            if (left_count[i - 1] == 0 || acc == 0)
                continue;
            cost = left_area[i - 1] * left_count[i - 1] + box_area (accmin, accmax) * acc;
            if (cost < best_cost)
            {
                best_cost = cost;
                best_split = i;
                /* child boxes come straight from the bins */
                memcpy (best_min[0], left_min[i - 1], sizeof (accmin));
                memcpy (best_max[0], left_max[i - 1], sizeof (accmax));
                memcpy (best_min[1], accmin, sizeof (accmin));
                memcpy (best_max[1], accmax, sizeof (accmax));
            }
        }
    }

    if (best_split > 0)
    {
        /* partition order[] by bin index */
        lo = first;
        hi = first + count;
        while (lo < hi)
        {
            guint t;
            guint b;

            t = self->order[lo];
            b = (guint) (((bd->centroid[t * 3 + axis] - cmin[axis]) / extent) * (gfloat) BVH_BINS);
            if (b >= BVH_BINS)
                b = BVH_BINS - 1;
            if (b < best_split)
            {
                lo++;
            }
            else
            {
                hi--;
                self->order[lo] = self->order[hi];
                self->order[hi] = t;
            }
        }
        left_n = lo - first;
        have_bounds = TRUE;
    }
    else
    {
        /* all centroids coincide: split the list in half */
        left_n = count / 2;
    }

    if (left_n == 0 || left_n == count)
    {
        left_n = count / 2;
        have_bounds = FALSE;
    }

    /* children are appended consecutively */
    {
        BvhNode *left;
        BvhNode *right;
        guint child;

        child = self->n_nodes;
        self->n_nodes += 2;
        node = &self->nodes[ni];
        node->first = child;
        node->count = 0;

        left = &self->nodes[child];
        right = &self->nodes[child + 1];
        left->first = first;
        left->count = left_n;
        right->first = first + left_n;
        right->count = count - left_n;
        if (have_bounds)
        {
            memcpy (left->bmin, best_min[0], sizeof (left->bmin));
            memcpy (left->bmax, best_max[0], sizeof (left->bmax));
            memcpy (right->bmin, best_min[1], sizeof (right->bmin));
            memcpy (right->bmax, best_max[1], sizeof (right->bmax));
        }
        else
        {
            node_bounds (self, bd, left);
            node_bounds (self, bd, right);
        }
    }
    return TRUE;
}

static void
build_hierarchy (LrgCollisionMesh *self)
{
    BuildData bd;
    guint i;
    guint *stack;
    guint *depth;
    guint top;

    g_clear_pointer (&self->nodes, g_free);
    g_clear_pointer (&self->order, g_free);
    self->n_nodes = 0;
    self->n_order = 0;
    self->max_depth = 0;
    if (self->n_triangles == 0)
        return;

    bd.tri_min = g_new (gfloat, (gsize) self->n_triangles * 3);
    bd.tri_max = g_new (gfloat, (gsize) self->n_triangles * 3);
    bd.centroid = g_new (gfloat, (gsize) self->n_triangles * 3);
    self->order = g_new (guint, self->n_triangles);

    /* only triangles with area enter the hierarchy */
    for (i = 0; i < self->n_triangles; i++)
    {
        gdouble tri[9];
        gdouble n[3];
        guint k;

        triangle_load (self, i, tri);
        if (!lrg_collision_triangle_normal (tri, n))
            continue;
        for (k = 0; k < 3; k++)
        {
            gfloat a;
            gfloat b;
            gfloat c;

            a = self->vertices[(gsize) i * 9 + k];
            b = self->vertices[(gsize) i * 9 + 3 + k];
            c = self->vertices[(gsize) i * 9 + 6 + k];
            bd.tri_min[i * 3 + k] = MIN (a, MIN (b, c));
            bd.tri_max[i * 3 + k] = MAX (a, MAX (b, c));
            bd.centroid[i * 3 + k] = (a + b + c) / 3.0f;
        }
        self->order[self->n_order++] = i;
    }

    if (self->n_order > 0)
    {
        self->nodes = g_new (BvhNode, (gsize) self->n_order * 2);
        self->nodes[0].first = 0;
        self->nodes[0].count = self->n_order;
        node_bounds (self, &bd, &self->nodes[0]);
        self->n_nodes = 1;

        /* depth-first split with an explicit stack */
        stack = g_new (guint, (gsize) self->n_order * 2 + 2);
        depth = g_new (guint, (gsize) self->n_order * 2 + 2);
        top = 0;
        stack[top] = 0;
        depth[top] = 1;
        top++;
        while (top > 0)
        {
            guint ni;
            guint d;

            top--;
            ni = stack[top];
            d = depth[top];
            self->max_depth = MAX (self->max_depth, d);
            if (node_split (self, &bd, ni))
            {
                guint child;

                child = self->nodes[ni].first;
                stack[top] = child + 1;
                depth[top] = d + 1;
                top++;
                stack[top] = child;
                depth[top] = d + 1;
                top++;
            }
        }
        g_free (stack);
        g_free (depth);
    }

    g_free (bd.tri_min);
    g_free (bd.tri_max);
    g_free (bd.centroid);
}

gboolean
lrg_collision_mesh_set_triangles (LrgCollisionMesh  *self,
                                  const gfloat      *vertices,
                                  guint              n_floats,
                                  const guint32     *tags,
                                  GError           **error)
{
    guint n;
    guint i;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (vertices != NULL || n_floats == 0, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* validate everything before touching the mesh */
    if (n_floats % 9 != 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "vertex float count %u is not a multiple of nine", n_floats);
        return FALSE;
    }
    n = n_floats / 9;
    if (n > LRG_COLLISION_MESH_MAX_TRIANGLES)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "%u triangles exceed the limit of %u", n,
                     (guint) LRG_COLLISION_MESH_MAX_TRIANGLES);
        return FALSE;
    }
    for (i = 0; i < n_floats; i++)
    {
        if (!isfinite (vertices[i]))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "vertex coordinate %u (triangle %u) is not finite", i, i / 9);
            return FALSE;
        }
    }

    g_clear_pointer (&self->vertices, g_free);
    g_clear_pointer (&self->tags, g_free);
    self->n_triangles = n;
    if (n > 0)
    {
        self->vertices = g_memdup2 (vertices, sizeof (gfloat) * n_floats);
        self->tags = g_new0 (guint32, n);
        if (tags != NULL)
            memcpy (self->tags, tags, sizeof (guint32) * n);
    }
    build_hierarchy (self);
    return TRUE;
}

guint
lrg_collision_mesh_get_triangle_count (LrgCollisionMesh *self)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), 0);

    return self->n_triangles;
}

guint
lrg_collision_mesh_get_node_count (LrgCollisionMesh *self)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), 0);

    return self->n_nodes;
}

/* ------------------------------------------------------------------------ */
/* Bounds                                                                   */
/* ------------------------------------------------------------------------ */

static gboolean mesh_bounds (LrgCollisionMesh *self,
                             gdouble          *bmin,
                             gdouble          *bmax);

/* World box of an instance: its shape's box through the transform. */
static gboolean
instance_bounds (const Instance *inst,
                 gdouble        *bmin,
                 gdouble        *bmax)
{
    gdouble smin[3];
    gdouble smax[3];
    guint corner;

    if (!mesh_bounds (inst->shape, smin, smax))
        return FALSE;

    bmin[0] = bmin[1] = bmin[2] = G_MAXDOUBLE;
    bmax[0] = bmax[1] = bmax[2] = -G_MAXDOUBLE;
    for (corner = 0; corner < 8; corner++)
    {
        gdouble p[3];
        gdouble w[3];
        guint k;

        p[0] = (corner & 1) ? smax[0] : smin[0];
        p[1] = (corner & 2) ? smax[1] : smin[1];
        p[2] = (corner & 4) ? smax[2] : smin[2];
        m4_point (inst->m, p, w);
        for (k = 0; k < 3; k++)
        {
            bmin[k] = MIN (bmin[k], w[k]);
            bmax[k] = MAX (bmax[k], w[k]);
        }
    }
    return TRUE;
}

static gboolean
mesh_bounds (LrgCollisionMesh *self,
             gdouble          *bmin,
             gdouble          *bmax)
{
    gboolean any;
    guint i;
    guint k;

    any = FALSE;
    bmin[0] = bmin[1] = bmin[2] = G_MAXDOUBLE;
    bmax[0] = bmax[1] = bmax[2] = -G_MAXDOUBLE;
    if (self->n_nodes > 0)
    {
        for (k = 0; k < 3; k++)
        {
            bmin[k] = self->nodes[0].bmin[k];
            bmax[k] = self->nodes[0].bmax[k];
        }
        any = TRUE;
    }
    for (i = 0; i < self->instances->len; i++)
    {
        const Instance *inst;
        gdouble imin[3];
        gdouble imax[3];

        inst = &g_array_index (self->instances, Instance, i);
        if (inst->shape == NULL || !instance_bounds (inst, imin, imax))
            continue;
        for (k = 0; k < 3; k++)
        {
            bmin[k] = MIN (bmin[k], imin[k]);
            bmax[k] = MAX (bmax[k], imax[k]);
        }
        any = TRUE;
    }
    return any;
}

gboolean
lrg_collision_mesh_get_bounds (LrgCollisionMesh *self,
                               gfloat           *min,
                               gfloat           *max)
{
    gdouble bmin[3];
    gdouble bmax[3];
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (min != NULL && max != NULL, FALSE);

    if (!mesh_bounds (self, bmin, bmax))
    {
        memset (min, 0, sizeof (gfloat) * 3);
        memset (max, 0, sizeof (gfloat) * 3);
        return FALSE;
    }
    for (k = 0; k < 3; k++)
    {
        min[k] = (gfloat) bmin[k];
        max[k] = (gfloat) bmax[k];
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Translation and instances                                                */
/* ------------------------------------------------------------------------ */

void
lrg_collision_mesh_translate (LrgCollisionMesh *self,
                              const gfloat     *offset)
{
    guint i;
    guint k;

    g_return_if_fail (LRG_IS_COLLISION_MESH (self));
    g_return_if_fail (offset != NULL);

    if (!v3f_finite (offset))
        return;

    /* own triangles and the hierarchy move together */
    for (i = 0; i < self->n_triangles; i++)
    {
        for (k = 0; k < 9; k++)
            self->vertices[(gsize) i * 9 + k] += offset[k % 3];
    }
    for (i = 0; i < self->n_nodes; i++)
    {
        for (k = 0; k < 3; k++)
        {
            self->nodes[i].bmin[k] += offset[k];
            self->nodes[i].bmax[k] += offset[k];
        }
    }

    /* instances: shift the translation and refresh the inverse */
    for (i = 0; i < self->instances->len; i++)
    {
        Instance *inst;

        inst = &g_array_index (self->instances, Instance, i);
        if (inst->shape == NULL)
            continue;
        inst->m[12] += offset[0];
        inst->m[13] += offset[1];
        inst->m[14] += offset[2];
        m4_affine_inverse (inst->m, inst->inv);
    }
}

/* Loads a float matrix and its inverse; FALSE when unusable. */
static gboolean
load_transform (const gfloat *transform,
                gdouble      *m,
                gdouble      *inv)
{
    guint k;

    for (k = 0; k < 16; k++)
        m[k] = (gdouble) transform[k];

    /* only affine transforms are meaningful for collision */
    if (fabs (m[3]) > 1e-6 || fabs (m[7]) > 1e-6 || fabs (m[11]) > 1e-6 ||
        fabs (m[15] - 1.0) > 1e-6)
        return FALSE;
    m[3] = 0.0;
    m[7] = 0.0;
    m[11] = 0.0;
    m[15] = 1.0;
    return m4_affine_inverse (m, inv);
}

guint
lrg_collision_mesh_add_instance (LrgCollisionMesh *self,
                                 LrgCollisionMesh *shape,
                                 const gfloat     *transform,
                                 guint64           instance_id)
{
    Instance inst;
    guint i;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), LRG_COLLISION_MESH_INVALID_INSTANCE);
    g_return_val_if_fail (LRG_IS_COLLISION_MESH (shape), LRG_COLLISION_MESH_INVALID_INSTANCE);
    g_return_val_if_fail (transform != NULL, LRG_COLLISION_MESH_INVALID_INSTANCE);

    if (shape == self)
        return LRG_COLLISION_MESH_INVALID_INSTANCE;

    memset (&inst, 0, sizeof (inst));
    if (!load_transform (transform, inst.m, inst.inv))
        return LRG_COLLISION_MESH_INVALID_INSTANCE;
    inst.id = instance_id;
    inst.shape = (LrgCollisionMesh *) g_object_ref (shape);

    /* reuse the lowest free slot so handles stay small and deterministic */
    for (i = 0; i < self->instances->len; i++)
    {
        if (g_array_index (self->instances, Instance, i).shape == NULL)
        {
            g_array_index (self->instances, Instance, i) = inst;
            self->n_live++;
            return i;
        }
    }
    g_array_append_val (self->instances, inst);
    self->n_live++;
    return self->instances->len - 1;
}

gboolean
lrg_collision_mesh_set_instance_transform (LrgCollisionMesh *self,
                                           guint             instance,
                                           const gfloat     *transform)
{
    Instance *inst;
    gdouble m[16];
    gdouble inv[16];

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (transform != NULL, FALSE);

    if (instance >= self->instances->len)
        return FALSE;
    inst = &g_array_index (self->instances, Instance, instance);
    if (inst->shape == NULL)
        return FALSE;
    if (!load_transform (transform, m, inv))
        return FALSE;
    memcpy (inst->m, m, sizeof (m));
    memcpy (inst->inv, inv, sizeof (inv));
    return TRUE;
}

gboolean
lrg_collision_mesh_remove_instance (LrgCollisionMesh *self,
                                    guint             instance)
{
    Instance *inst;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);

    if (instance >= self->instances->len)
        return FALSE;
    inst = &g_array_index (self->instances, Instance, instance);
    if (inst->shape == NULL)
        return FALSE;
    g_clear_object (&inst->shape);
    self->n_live--;

    /* trim trailing free slots */
    while (self->instances->len > 0 &&
           g_array_index (self->instances, Instance, self->instances->len - 1).shape == NULL)
        g_array_set_size (self->instances, self->instances->len - 1);
    return TRUE;
}

guint
lrg_collision_mesh_get_instance_count (LrgCollisionMesh *self)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), 0);

    return self->n_live;
}

/* ------------------------------------------------------------------------ */
/* Ray cast                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gdouble  t;            /* nearest hit so far (ray parameter) */
    gdouble  normal[3];    /* raw face normal, query space */
    gdouble  point[3];
    guint    triangle;
    guint32  tag;
    guint64  instance_id;
    gboolean hit;
} RayResult;

/* Slab test; returns the entry distance or -1 when the box is missed. */
static gdouble
ray_box (const gdouble *o,
         const gdouble *inv_d,
         const gfloat  *bmin,
         const gfloat  *bmax,
         gdouble        tmax)
{
    gdouble t0;
    gdouble t1;
    guint k;

    t0 = 0.0;
    t1 = tmax;
    for (k = 0; k < 3; k++)
    {
        gdouble a;
        gdouble b;

        a = ((gdouble) bmin[k] - o[k]) * inv_d[k];
        b = ((gdouble) bmax[k] - o[k]) * inv_d[k];
        if (isnan (a) || isnan (b))
        {
            /* parallel ray on a slab boundary: inside when within it */
            if (o[k] < bmin[k] || o[k] > bmax[k])
                return -1.0;
            continue;
        }
        if (a > b)
        {
            gdouble tmp;

            tmp = a;
            a = b;
            b = tmp;
        }
        t0 = MAX (t0, a);
        t1 = MIN (t1, b);
        if (t0 > t1)
            return -1.0;
    }
    return t0;
}

/* Double-sided Moller-Trumbore. */
static gboolean
ray_triangle (const gdouble *o,
              const gdouble *d,
              const gdouble *tri,
              gdouble       *out_t)
{
    gdouble e1[3];
    gdouble e2[3];
    gdouble p[3];
    gdouble s[3];
    gdouble q[3];
    gdouble det;
    gdouble inv;
    gdouble u;
    gdouble v;
    gdouble t;

    v3_sub (tri + 3, tri, e1);
    v3_sub (tri + 6, tri, e2);
    v3_cross (d, e2, p);
    det = v3_dot (e1, p);
    if (!(fabs (det) > 1e-14 * v3_length (e1) * v3_length (e2) * v3_length (d)))
        return FALSE;
    inv = 1.0 / det;
    v3_sub (o, tri, s);
    u = v3_dot (s, p) * inv;
    if (u < 0.0 || u > 1.0)
        return FALSE;
    v3_cross (s, e1, q);
    v = v3_dot (d, q) * inv;
    if (v < 0.0 || u + v > 1.0)
        return FALSE;
    t = v3_dot (e2, q) * inv;
    if (t < 0.0)
        return FALSE;
    *out_t = t;
    return TRUE;
}

static void raycast_mesh (LrgCollisionMesh *self,
                          const gdouble    *o,
                          const gdouble    *d,
                          RayResult        *res);

static void
raycast_own (LrgCollisionMesh *self,
             const gdouble    *o,
             const gdouble    *d,
             RayResult        *res)
{
    gdouble inv_d[3];
    guint local[BVH_STACK_LOCAL];
    guint *stack;
    guint top;
    guint k;

    if (self->n_nodes == 0)
        return;

    for (k = 0; k < 3; k++)
        inv_d[k] = 1.0 / d[k];

    stack = self->max_depth + 2 <= BVH_STACK_LOCAL ? local : g_new (guint, self->max_depth + 2);
    top = 0;
    stack[top++] = 0;
    while (top > 0)
    {
        const BvhNode *node;

        node = &self->nodes[stack[--top]];
        if (ray_box (o, inv_d, node->bmin, node->bmax, res->t) < 0.0)
            continue;

        if (node->count > 0)
        {
            guint i;

            for (i = node->first; i < node->first + node->count; i++)
            {
                gdouble tri[9];
                gdouble t;
                guint ti;

                ti = self->order[i];
                triangle_load (self, ti, tri);
                if (ray_triangle (o, d, tri, &t) && t < res->t)
                {
                    gdouble e1[3];
                    gdouble e2[3];

                    res->t = t;
                    res->hit = TRUE;
                    res->triangle = ti;
                    res->tag = self->tags[ti];
                    res->instance_id = 0;
                    v3_sub (tri + 3, tri, e1);
                    v3_sub (tri + 6, tri, e2);
                    v3_cross (e1, e2, res->normal);
                    v3_add_scaled (o, d, t, res->point);
                }
            }
        }
        else
        {
            const BvhNode *a;
            const BvhNode *b;
            gdouble ta;
            gdouble tb;

            /* push the farther child first so the nearer pops first */
            a = &self->nodes[node->first];
            b = &self->nodes[node->first + 1];
            ta = ray_box (o, inv_d, a->bmin, a->bmax, res->t);
            tb = ray_box (o, inv_d, b->bmin, b->bmax, res->t);
            if (ta >= 0.0 && tb >= 0.0)
            {
                if (ta <= tb)
                {
                    stack[top++] = node->first + 1;
                    stack[top++] = node->first;
                }
                else
                {
                    stack[top++] = node->first;
                    stack[top++] = node->first + 1;
                }
            }
            else if (ta >= 0.0)
            {
                stack[top++] = node->first;
            }
            else if (tb >= 0.0)
            {
                stack[top++] = node->first + 1;
            }
        }
    }
    if (stack != local)
        g_free (stack);
}

static void
raycast_mesh (LrgCollisionMesh *self,
              const gdouble    *o,
              const gdouble    *d,
              RayResult        *res)
{
    guint i;

    raycast_own (self, o, d, res);

    for (i = 0; i < self->instances->len; i++)
    {
        const Instance *inst;
        gdouble lo[3];
        gdouble ld[3];
        RayResult sub;

        inst = &g_array_index (self->instances, Instance, i);
        if (inst->shape == NULL)
            continue;

        /* local ray keeps the world parameterisation: t stays a world
         * distance because the world direction is unit length */
        m4_point (inst->inv, o, lo);
        m4_vector (inst->inv, d, ld);
        memset (&sub, 0, sizeof (sub));
        sub.t = res->t;
        raycast_mesh (inst->shape, lo, ld, &sub);
        if (sub.hit && sub.t < res->t)
        {
            gdouble n[3];

            res->t = sub.t;
            res->hit = TRUE;
            res->triangle = sub.triangle;
            res->tag = sub.tag;
            res->instance_id = inst->id;
            m4_point (inst->m, sub.point, res->point);

            /* normals transform by the inverse transpose */
            n[0] = inst->inv[0] * sub.normal[0] + inst->inv[1] * sub.normal[1] + inst->inv[2] * sub.normal[2];
            n[1] = inst->inv[4] * sub.normal[0] + inst->inv[5] * sub.normal[1] + inst->inv[6] * sub.normal[2];
            n[2] = inst->inv[8] * sub.normal[0] + inst->inv[9] * sub.normal[1] + inst->inv[10] * sub.normal[2];
            memcpy (res->normal, n, sizeof (n));
        }
    }
}

static void
hit_store (LrgCollisionHit *out,
           const gdouble   *position,
           const gdouble   *normal,
           gdouble          distance,
           guint            triangle,
           guint32          tag,
           guint64          instance_id)
{
    guint k;

    for (k = 0; k < 3; k++)
    {
        out->position[k] = (gfloat) position[k];
        out->normal[k] = (gfloat) normal[k];
    }
    out->distance = (gfloat) distance;
    out->triangle = triangle;
    out->tag = tag;
    out->instance_id = instance_id;
}

gboolean
lrg_collision_mesh_raycast (LrgCollisionMesh *self,
                            const gfloat     *origin,
                            const gfloat     *direction,
                            gfloat            max_distance,
                            LrgCollisionHit  *out_hit)
{
    gdouble o[3];
    gdouble d[3];
    gdouble len;
    RayResult res;
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);

    if (out_hit != NULL)
        memset (out_hit, 0, sizeof (*out_hit));
    if (!v3f_finite (origin) || !v3f_finite (direction) || !isfinite (max_distance) ||
        max_distance <= 0.0f)
        return FALSE;

    for (k = 0; k < 3; k++)
    {
        o[k] = origin[k];
        d[k] = direction[k];
    }
    len = v3_length (d);
    if (!(len > 1e-12))
        return FALSE;
    for (k = 0; k < 3; k++)
        d[k] /= len;

    memset (&res, 0, sizeof (res));
    res.t = max_distance;
    raycast_mesh (self, o, d, &res);
    if (!res.hit)
        return FALSE;

    if (out_hit != NULL)
    {
        gdouble n[3];
        gdouble nl;

        /* unit normal facing back along the ray */
        memcpy (n, res.normal, sizeof (n));
        nl = v3_length (n);
        if (nl > 0.0)
        {
            for (k = 0; k < 3; k++)
                n[k] /= nl;
        }
        if (v3_dot (n, d) > 0.0)
        {
            for (k = 0; k < 3; k++)
                n[k] = -n[k];
        }
        hit_store (out_hit, res.point, n, res.t, res.triangle, res.tag, res.instance_id);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Box queries                                                              */
/* ------------------------------------------------------------------------ */

static gboolean
box_overlap (const gfloat  *bmin,
             const gfloat  *bmax,
             const gdouble *qmin,
             const gdouble *qmax)
{
    return !(bmax[0] < qmin[0] || bmin[0] > qmax[0] ||
             bmax[1] < qmin[1] || bmin[1] > qmax[1] ||
             bmax[2] < qmin[2] || bmin[2] > qmax[2]);
}

/*
 * Recursive box query. world is the model-to-world matrix of self (NULL
 * for identity); qmin/qmax are in self's model space; id_override is the
 * outermost instance id (0 at the top).
 */
static gboolean
query_box_mesh (LrgCollisionMesh    *self,
                const gdouble       *world,
                const gdouble       *qmin,
                const gdouble       *qmax,
                guint64              id_override,
                LrgCollisionTriFunc  func,
                gpointer             user_data)
{
    guint i;

    /* own triangles through the hierarchy */
    if (self->n_nodes > 0)
    {
        guint local[BVH_STACK_LOCAL];
        guint *stack;
        guint top;
        gboolean keep_going;

        keep_going = TRUE;
        stack = self->max_depth + 2 <= BVH_STACK_LOCAL ? local : g_new (guint, self->max_depth + 2);
        top = 0;
        stack[top++] = 0;
        while (top > 0 && keep_going)
        {
            const BvhNode *node;

            node = &self->nodes[stack[--top]];
            if (!box_overlap (node->bmin, node->bmax, qmin, qmax))
                continue;
            if (node->count == 0)
            {
                stack[top++] = node->first + 1;
                stack[top++] = node->first;
                continue;
            }
            for (i = node->first; i < node->first + node->count && keep_going; i++)
            {
                LrgCollisionTri tri;
                gfloat tmin[3];
                gfloat tmax[3];
                guint ti;
                guint k;

                ti = self->order[i];
                for (k = 0; k < 3; k++)
                {
                    gfloat a;
                    gfloat b;
                    gfloat c;

                    a = self->vertices[(gsize) ti * 9 + k];
                    b = self->vertices[(gsize) ti * 9 + 3 + k];
                    c = self->vertices[(gsize) ti * 9 + 6 + k];
                    tmin[k] = MIN (a, MIN (b, c));
                    tmax[k] = MAX (a, MAX (b, c));
                }
                if (!box_overlap (tmin, tmax, qmin, qmax))
                    continue;

                triangle_load (self, ti, tri.v);
                if (world != NULL)
                {
                    m4_point (world, tri.v, tri.v);
                    m4_point (world, tri.v + 3, tri.v + 3);
                    m4_point (world, tri.v + 6, tri.v + 6);
                }
                tri.tag = self->tags[ti];
                tri.triangle = ti;
                tri.instance_id = id_override;
                keep_going = func (&tri, user_data);
            }
        }
        if (stack != local)
            g_free (stack);
        if (!keep_going)
            return FALSE;
    }

    /* instances: transform the query box into the shape's space */
    for (i = 0; i < self->instances->len; i++)
    {
        const Instance *inst;
        gdouble lmin[3];
        gdouble lmax[3];
        gdouble composed[16];
        guint corner;
        guint k;

        inst = &g_array_index (self->instances, Instance, i);
        if (inst->shape == NULL)
            continue;

        lmin[0] = lmin[1] = lmin[2] = G_MAXDOUBLE;
        lmax[0] = lmax[1] = lmax[2] = -G_MAXDOUBLE;
        for (corner = 0; corner < 8; corner++)
        {
            gdouble p[3];
            gdouble l[3];

            p[0] = (corner & 1) ? qmax[0] : qmin[0];
            p[1] = (corner & 2) ? qmax[1] : qmin[1];
            p[2] = (corner & 4) ? qmax[2] : qmin[2];
            m4_point (inst->inv, p, l);
            for (k = 0; k < 3; k++)
            {
                lmin[k] = MIN (lmin[k], l[k]);
                lmax[k] = MAX (lmax[k], l[k]);
            }
        }

        if (world != NULL)
            m4_multiply (world, inst->m, composed);
        else
            memcpy (composed, inst->m, sizeof (composed));

        if (!query_box_mesh (inst->shape, composed, lmin, lmax,
                             id_override != 0 ? id_override : inst->id, func, user_data))
            return FALSE;
    }
    return TRUE;
}

gboolean
lrg_collision_mesh_query_box (LrgCollisionMesh    *self,
                              const gdouble       *min,
                              const gdouble       *max,
                              LrgCollisionTriFunc  func,
                              gpointer             user_data)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (min != NULL && max != NULL && func != NULL, FALSE);

    return query_box_mesh (self, NULL, min, max, 0, func, user_data);
}

/* ------------------------------------------------------------------------ */
/* Sweeps and overlaps                                                      */
/* ------------------------------------------------------------------------ */

typedef struct
{
    const gdouble   *a;
    const gdouble   *b;
    gdouble          r;
    const gdouble   *m;
    gdouble          best_t;
    gdouble          tie;          /* fraction window treated as a tie */
    gdouble          normal[3];
    gdouble          face[3];
    gdouble          point[3];
    LrgCollisionTri  tri;
    gboolean         hit;
} SweepData;

static gboolean
sweep_visit (const LrgCollisionTri *tri,
             gpointer               user_data)
{
    SweepData *sd;
    gdouble t;
    gdouble n[3];
    gdouble p[3];

    gdouble face[3];
    gboolean take;

    sd = (SweepData *) user_data;
    if (!sweep_triangle (sd->a, sd->b, sd->r, sd->m, tri->v, &t, n, p))
        return TRUE;

    /* the face normal on the contact's side */
    lrg_collision_triangle_normal (tri->v, face);
    if (v3_dot (face, n) < 0.0)
    {
        face[0] = -face[0];
        face[1] = -face[1];
        face[2] = -face[2];
    }

    /* earliest contact wins; among simultaneous ones the most upward
     * face (a step's tread beats its riser) */
    if (!sd->hit || t < sd->best_t - sd->tie)
        take = TRUE;
    else if (t <= sd->best_t + sd->tie && face[1] > sd->face[1] + 1e-9)
        take = TRUE;
    else
        take = FALSE;

    if (take)
    {
        /* a tie keeps the earlier time so nothing moves past a contact */
        if (!sd->hit || t < sd->best_t)
            sd->best_t = t;
        sd->hit = TRUE;
        memcpy (sd->normal, n, sizeof (n));
        memcpy (sd->face, face, sizeof (face));
        memcpy (sd->point, p, sizeof (p));
        sd->tri = *tri;
    }
    return TRUE;
}

gboolean
lrg_collision_mesh_sweep_segment (LrgCollisionMesh *self,
                                  const gdouble    *a,
                                  const gdouble    *b,
                                  gdouble           radius,
                                  const gdouble    *motion,
                                  LrgCollisionHit  *out_hit,
                                  gdouble          *out_fraction,
                                  gdouble          *out_face_normal)
{
    SweepData sd;
    gdouble qmin[3];
    gdouble qmax[3];
    gdouble mlen;
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);

    mlen = v3_length (motion);
    if (!(mlen > 1e-12) || !isfinite (mlen) || !(radius > 0.0) || !isfinite (radius))
        return FALSE;

    /* the swept volume's box */
    for (k = 0; k < 3; k++)
    {
        gdouble lo;
        gdouble hi;

        lo = MIN (a[k], b[k]);
        hi = MAX (a[k], b[k]);
        qmin[k] = MIN (lo, lo + motion[k]) - radius - 1e-4;
        qmax[k] = MAX (hi, hi + motion[k]) + radius + 1e-4;
    }

    memset (&sd, 0, sizeof (sd));
    sd.a = a;
    sd.b = b;
    sd.r = radius;
    sd.m = motion;
    sd.tie = 1e-5 / mlen;
    lrg_collision_mesh_query_box (self, qmin, qmax, sweep_visit, &sd);
    if (!sd.hit)
        return FALSE;

    if (out_hit != NULL)
        hit_store (out_hit, sd.point, sd.normal, sd.best_t * mlen,
                   sd.tri.triangle, sd.tri.tag, sd.tri.instance_id);
    if (out_fraction != NULL)
        *out_fraction = sd.best_t;
    if (out_face_normal != NULL)
        memcpy (out_face_normal, sd.face, sizeof (sd.face));
    return TRUE;
}

/* Axis of an upright capsule: segment between the two sphere centres. */
static void
capsule_axis (const gfloat *base,
              gfloat        radius,
              gfloat        height,
              gdouble      *a,
              gdouble      *b)
{
    gdouble top;

    top = MAX ((gdouble) height - radius, (gdouble) radius);
    a[0] = base[0];
    a[1] = (gdouble) base[1] + radius;
    a[2] = base[2];
    b[0] = base[0];
    b[1] = (gdouble) base[1] + top;
    b[2] = base[2];
}

gboolean
lrg_collision_mesh_sweep_sphere (LrgCollisionMesh *self,
                                 const gfloat     *center,
                                 gfloat            radius,
                                 const gfloat     *motion,
                                 LrgCollisionHit  *out_hit)
{
    gdouble c[3];
    gdouble m[3];
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (center != NULL && motion != NULL, FALSE);

    if (out_hit != NULL)
        memset (out_hit, 0, sizeof (*out_hit));
    if (!v3f_finite (center) || !v3f_finite (motion))
        return FALSE;
    for (k = 0; k < 3; k++)
    {
        c[k] = center[k];
        m[k] = motion[k];
    }
    return lrg_collision_mesh_sweep_segment (self, c, c, radius, m, out_hit, NULL, NULL);
}

gboolean
lrg_collision_mesh_sweep_capsule (LrgCollisionMesh *self,
                                  const gfloat     *base,
                                  gfloat            radius,
                                  gfloat            height,
                                  const gfloat     *motion,
                                  LrgCollisionHit  *out_hit)
{
    gdouble a[3];
    gdouble b[3];
    gdouble m[3];
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (base != NULL && motion != NULL, FALSE);

    if (out_hit != NULL)
        memset (out_hit, 0, sizeof (*out_hit));
    if (!v3f_finite (base) || !v3f_finite (motion) || !isfinite (height))
        return FALSE;
    capsule_axis (base, radius, height, a, b);
    for (k = 0; k < 3; k++)
        m[k] = motion[k];
    return lrg_collision_mesh_sweep_segment (self, a, b, radius, m, out_hit, NULL, NULL);
}

typedef struct
{
    const gdouble *a;
    const gdouble *b;
    gdouble        r;
    gboolean       overlap;
} OverlapData;

static gboolean
overlap_visit (const LrgCollisionTri *tri,
               gpointer               user_data)
{
    OverlapData *od;
    gdouble cs[3];
    gdouble ct[3];

    od = (OverlapData *) user_data;
    if (lrg_collision_segment_triangle (od->a, od->b, tri->v, cs, ct) < od->r - DIST_EPS)
    {
        od->overlap = TRUE;
        return FALSE;
    }
    return TRUE;
}

gboolean
lrg_collision_mesh_overlap_capsule (LrgCollisionMesh *self,
                                    const gfloat     *base,
                                    gfloat            radius,
                                    gfloat            height)
{
    OverlapData od;
    gdouble a[3];
    gdouble b[3];
    gdouble qmin[3];
    gdouble qmax[3];
    guint k;

    g_return_val_if_fail (LRG_IS_COLLISION_MESH (self), FALSE);
    g_return_val_if_fail (base != NULL, FALSE);

    if (!v3f_finite (base) || !isfinite (radius) || !isfinite (height) || !(radius > 0.0f))
        return FALSE;
    capsule_axis (base, radius, height, a, b);
    for (k = 0; k < 3; k++)
    {
        qmin[k] = MIN (a[k], b[k]) - radius;
        qmax[k] = MAX (a[k], b[k]) + radius;
    }
    od.a = a;
    od.b = b;
    od.r = radius;
    od.overlap = FALSE;
    lrg_collision_mesh_query_box (self, qmin, qmax, overlap_visit, &od);
    return od.overlap;
}
