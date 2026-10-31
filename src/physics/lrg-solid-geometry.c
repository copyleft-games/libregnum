/* lrg-solid-geometry.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Closed 3D solids. Points are classified against the volume, not against
 * a triangle shell. Y is up. Transforms are column-major, translation in
 * elements 12..14, and must be a rotation plus a positive uniform scale.
 *
 * Supported overlap pairs are analytic for spheres and sphyls, and
 * separating-axis for boxes and convex hulls. SAT tests every face
 * normal and every unique edge-direction cross; it does not drop axes.
 * A sphyl against a box or a convex hull is an explicit error, including
 * when an aggregate hides that pair. Sphere sweeps use a quadratic, a
 * rounded box, a capsule, or conservative advancement against a hull.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-solid-private.h"

#define SOLID_EPS (1e-5)
#define SOLID_ORIENT_FLOOR (1e-12)

static GType
register_enum (const gchar *name, const GEnumValue *values)
{
    static gsize guard_shape = 0;
    static gsize guard_class = 0;
    gsize *guard;
    GType registered;

    guard = (strcmp (name, "LrgSolidShape") == 0) ? &guard_shape : &guard_class;
    if (g_once_init_enter (guard))
    {
        registered = g_enum_register_static (name, values);
        g_once_init_leave (guard, (gsize) registered);
    }
    return (GType) *guard;
}

GType
lrg_solid_shape_get_type (void)
{
    static const GEnumValue values[] = {
        { LRG_SOLID_SHAPE_SPHERE, "LRG_SOLID_SHAPE_SPHERE", "sphere" },
        { LRG_SOLID_SHAPE_BOX, "LRG_SOLID_SHAPE_BOX", "box" },
        { LRG_SOLID_SHAPE_SPHYL, "LRG_SOLID_SHAPE_SPHYL", "sphyl" },
        { LRG_SOLID_SHAPE_CONVEX, "LRG_SOLID_SHAPE_CONVEX", "convex" },
        { LRG_SOLID_SHAPE_AGGREGATE, "LRG_SOLID_SHAPE_AGGREGATE", "aggregate" },
        { 0, NULL, NULL }
    };

    return register_enum ("LrgSolidShape", values);
}

GType
lrg_solid_class_get_type (void)
{
    static const GEnumValue values[] = {
        { LRG_SOLID_CLASS_OUTSIDE, "LRG_SOLID_CLASS_OUTSIDE", "outside" },
        { LRG_SOLID_CLASS_TOUCHING, "LRG_SOLID_CLASS_TOUCHING", "touching" },
        { LRG_SOLID_CLASS_INSIDE, "LRG_SOLID_CLASS_INSIDE", "inside" },
        { 0, NULL, NULL }
    };

    return register_enum ("LrgSolidClass", values);
}

G_DEFINE_FINAL_TYPE (LrgSolidGeometry, lrg_solid_geometry, G_TYPE_OBJECT)

static void
lrg_solid_geometry_finalize (GObject *object)
{
    LrgSolidGeometry *self;
    guint i;

    self = LRG_SOLID_GEOMETRY (object);
    lrg_solid_prims_free (self->prims, self->n_prims);
    self->prims = NULL;
    self->n_prims = 0;
    for (i = 0; i < self->n_children; i++)
        g_clear_object (&self->children[i]);
    g_free (self->children);
    self->children = NULL;
    self->n_children = 0;
    G_OBJECT_CLASS (lrg_solid_geometry_parent_class)->finalize (object);
}

static void
lrg_solid_geometry_class_init (LrgSolidGeometryClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_solid_geometry_finalize;
}

static void
lrg_solid_geometry_init (LrgSolidGeometry *self)
{
    self->shape = LRG_SOLID_SHAPE_SPHERE;
    self->depth = 1;
}

/* ------------------------------------------------------------------------ */
/* Vectors                                                                  */
/* ------------------------------------------------------------------------ */

static gdouble
v_dot (const gdouble *a, const gdouble *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
v_cross (const gdouble *a, const gdouble *b, gdouble *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static gdouble
v_len (const gdouble *a)
{
    return sqrt (v_dot (a, a));
}

static gboolean
v_finite3 (const gdouble *a)
{
    return isfinite (a[0]) && isfinite (a[1]) && isfinite (a[2]);
}

static gboolean
v_normalize (gdouble *a)
{
    gdouble length;

    length = v_len (a);
    if (!(length > 1e-15) || !isfinite (length))
        return FALSE;
    a[0] /= length;
    a[1] /= length;
    a[2] /= length;
    return TRUE;
}

static void
v_sub (const gdouble *a, const gdouble *b, gdouble *out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void
v_mad (const gdouble *origin, const gdouble *dir, gdouble scale, gdouble *out)
{
    out[0] = origin[0] + dir[0] * scale;
    out[1] = origin[1] + dir[1] * scale;
    out[2] = origin[2] + dir[2] * scale;
}

static void
float3 (const gdouble *in, gfloat *out)
{
    out[0] = (gfloat) in[0];
    out[1] = (gfloat) in[1];
    out[2] = (gfloat) in[2];
}

static const gchar *
prim_name (LrgSolidPrimKind kind)
{
    switch (kind)
    {
    case LRG_SOLID_PRIM_SPHERE: return "sphere";
    case LRG_SOLID_PRIM_BOX: return "box";
    case LRG_SOLID_PRIM_SPHYL: return "sphyl";
    case LRG_SOLID_PRIM_CONVEX: return "convex";
    default: return "unknown";
    }
}

void
lrg_solid_prims_free (LrgSolidPrim *prims, guint n_prims)
{
    guint i;

    if (prims == NULL)
        return;
    for (i = 0; i < n_prims; i++)
    {
        g_free (prims[i].points);
        g_free (prims[i].planes);
        g_free (prims[i].faces);
    }
    g_free (prims);
}

static gboolean
prim_copy (const LrgSolidPrim *src, LrgSolidPrim *dst)
{
    memset (dst, 0, sizeof (*dst));
    dst->kind = src->kind;
    memcpy (dst->center, src->center, sizeof dst->center);
    memcpy (dst->axis, src->axis, sizeof dst->axis);
    dst->radius = src->radius;
    memcpy (dst->half, src->half, sizeof dst->half);
    dst->n_points = src->n_points;
    dst->n_planes = src->n_planes;
    dst->n_faces = src->n_faces;
    if (src->n_points > 0)
    {
        dst->points = g_new (gdouble, src->n_points * 3);
        memcpy (dst->points, src->points, sizeof (gdouble) * src->n_points * 3);
    }
    if (src->n_planes > 0)
    {
        dst->planes = g_new (gdouble, src->n_planes * 4);
        memcpy (dst->planes, src->planes, sizeof (gdouble) * src->n_planes * 4);
    }
    if (src->n_faces > 0)
    {
        dst->faces = g_new (guint, src->n_faces * 3);
        memcpy (dst->faces, src->faces, sizeof (guint) * src->n_faces * 3);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Transforms                                                               */
/* ------------------------------------------------------------------------ */

gboolean
lrg_solid_transform_decompose (const gfloat *transform,
                               gdouble       rotation[3][3],
                               gdouble       translation[3],
                               gdouble      *scale,
                               GError      **error)
{
    gdouble column[3][3];
    gdouble length[3];
    gdouble det;
    gdouble dot01, dot02, dot12;
    guint c, i;

    g_return_val_if_fail (rotation != NULL, FALSE);
    g_return_val_if_fail (translation != NULL, FALSE);
    g_return_val_if_fail (scale != NULL, FALSE);
    if (transform == NULL)
    {
        for (c = 0; c < 3; c++)
        {
            translation[c] = 0.0;
            for (i = 0; i < 3; i++)
                rotation[c][i] = (c == i) ? 1.0 : 0.0;
        }
        *scale = 1.0;
        return TRUE;
    }
    for (i = 0; i < 16; i++)
    {
        if (!isfinite (transform[i]))
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Solid transform has a non-finite element");
            return FALSE;
        }
    }
    if (fabs ((gdouble) transform[3]) > 1e-5 || fabs ((gdouble) transform[7]) > 1e-5 ||
        fabs ((gdouble) transform[11]) > 1e-5 || fabs ((gdouble) transform[15] - 1.0) > 1e-5)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid transform must keep a 0,0,0,1 perspective row");
        return FALSE;
    }
    for (c = 0; c < 3; c++)
    {
        for (i = 0; i < 3; i++)
            column[c][i] = (gdouble) transform[c * 4 + i];
        length[c] = v_len (column[c]);
        if (!(length[c] > 1e-8) || !isfinite (length[c]))
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Solid transform scale is not positive");
            return FALSE;
        }
    }
    if (fabs (length[1] - length[0]) > 1e-5 * length[0] ||
        fabs (length[2] - length[0]) > 1e-5 * length[0])
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid transform rejects nonuniform scale");
        return FALSE;
    }
    for (c = 0; c < 3; c++)
        for (i = 0; i < 3; i++)
            rotation[c][i] = column[c][i] / length[0];
    dot01 = 0.0;
    dot02 = 0.0;
    dot12 = 0.0;
    for (i = 0; i < 3; i++)
    {
        dot01 += rotation[0][i] * rotation[1][i];
        dot02 += rotation[0][i] * rotation[2][i];
        dot12 += rotation[1][i] * rotation[2][i];
    }
    if (fabs (dot01) > 1e-4 || fabs (dot02) > 1e-4 || fabs (dot12) > 1e-4)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid transform rejects shear");
        return FALSE;
    }
    det = rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1]) -
          rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0]) +
          rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0]);
    if (!(det > 0.999))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid transform rejects reflection or a non-rotation");
        return FALSE;
    }
    translation[0] = (gdouble) transform[12];
    translation[1] = (gdouble) transform[13];
    translation[2] = (gdouble) transform[14];
    *scale = length[0];
    return TRUE;
}

static void
transform_point (const gdouble rotation[3][3],
                 gdouble       scale,
                 const gdouble translation[3],
                 const gdouble in[3],
                 gdouble       out[3])
{
    guint i, c;

    for (i = 0; i < 3; i++)
    {
        out[i] = translation[i];
        for (c = 0; c < 3; c++)
            out[i] += scale * rotation[c][i] * in[c];
    }
}

static void
rotate_vector (const gdouble rotation[3][3], const gdouble in[3], gdouble out[3])
{
    guint i, c;

    for (i = 0; i < 3; i++)
    {
        out[i] = 0.0;
        for (c = 0; c < 3; c++)
            out[i] += rotation[c][i] * in[c];
    }
}

static void
prim_transform (LrgSolidPrim  *prim,
                const gdouble  rotation[3][3],
                gdouble        scale,
                const gdouble  translation[3])
{
    gdouble next[3][3];
    gdouble point[3];
    gdouble normal[3];
    guint i, c, f;

    transform_point (rotation, scale, translation, prim->center, point);
    memcpy (prim->center, point, sizeof point);
    for (c = 0; c < 3; c++)
        rotate_vector (rotation, prim->axis[c], next[c]);
    memcpy (prim->axis, next, sizeof next);
    prim->radius *= scale;
    for (c = 0; c < 3; c++)
        prim->half[c] *= scale;
    for (i = 0; i < prim->n_points; i++)
    {
        transform_point (rotation, scale, translation, prim->points + (gsize) i * 3, point);
        memcpy (prim->points + (gsize) i * 3, point, sizeof point);
    }
    for (f = 0; f < prim->n_planes; f++)
    {
        rotate_vector (rotation, prim->planes + (gsize) f * 4, normal);
        if (!v_normalize (normal))
            continue;
        prim->planes[f * 4 + 0] = normal[0];
        prim->planes[f * 4 + 1] = normal[1];
        prim->planes[f * 4 + 2] = normal[2];
        prim->planes[f * 4 + 3] = prim->planes[f * 4 + 3] * scale + v_dot (normal, translation);
    }
}

static void
axis_identity (gdouble axis[3][3])
{
    guint c, i;

    for (c = 0; c < 3; c++)
        for (i = 0; i < 3; i++)
            axis[c][i] = (c == i) ? 1.0 : 0.0;
}

gboolean
lrg_solid_geometry_bake (LrgSolidGeometry *self,
                         const gdouble     rotation[3][3],
                         gdouble           scale,
                         const gdouble     translation[3],
                         LrgSolidPrim    **out_prims,
                         guint            *out_n,
                         GError          **error)
{
    LrgSolidPrim *copy;
    guint i;

    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), FALSE);
    g_return_val_if_fail (out_prims != NULL, FALSE);
    g_return_val_if_fail (out_n != NULL, FALSE);
    copy = g_new0 (LrgSolidPrim, self->n_prims);
    for (i = 0; i < self->n_prims; i++)
    {
        if (!prim_copy (&self->prims[i], &copy[i]))
        {
            lrg_solid_prims_free (copy, i + 1);
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "Solid bake could not copy a primitive");
            return FALSE;
        }
        prim_transform (&copy[i], rotation, scale, translation);
    }
    *out_prims = copy;
    *out_n = self->n_prims;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Convex hull                                                              */
/* ------------------------------------------------------------------------ */

static gdouble
orient_point (const gdouble *points, guint a, guint b, guint c, guint d)
{
    const gdouble *pa, *pb, *pc, *pd;
    gdouble ab[3], ac[3], ad[3], cross[3];

    pa = points + (gsize) a * 3;
    pb = points + (gsize) b * 3;
    pc = points + (gsize) c * 3;
    pd = points + (gsize) d * 3;
    v_sub (pb, pa, ab);
    v_sub (pc, pa, ac);
    v_sub (pd, pa, ad);
    v_cross (ab, ac, cross);
    return v_dot (cross, ad);
}

static gdouble
orient_xyz (const gdouble *a, const gdouble *b, const gdouble *c, const gdouble *d)
{
    gdouble ab[3], ac[3], ad[3], cross[3];

    v_sub (b, a, ab);
    v_sub (c, a, ac);
    v_sub (d, a, ad);
    v_cross (ab, ac, cross);
    return v_dot (cross, ad);
}

typedef struct
{
    guint a, b, c;
} SolidTri;

static gboolean
hull_add_point (const gdouble *points,
                guint          index,
                SolidTri      *faces,
                guint         *n_faces,
                guint          capacity,
                const gdouble  interior[3],
                gdouble        epsilon)
{
    gboolean *visible;
    guint *edge_a, *edge_b;
    gboolean *edge_dead;
    guint n_edges, i, j, n_visible, written;
    SolidTri *kept;

    visible = g_new0 (gboolean, *n_faces);
    n_visible = 0;
    for (i = 0; i < *n_faces; i++)
    {
        if (orient_point (points, faces[i].a, faces[i].b, faces[i].c, index) > epsilon)
        {
            visible[i] = TRUE;
            n_visible++;
        }
    }
    if (n_visible == 0)
    {
        g_free (visible);
        return TRUE;
    }
    n_edges = n_visible * 3;
    edge_a = g_new (guint, n_edges);
    edge_b = g_new (guint, n_edges);
    edge_dead = g_new0 (gboolean, n_edges);
    n_edges = 0;
    for (i = 0; i < *n_faces; i++)
    {
        if (!visible[i])
            continue;
        edge_a[n_edges] = faces[i].a;
        edge_b[n_edges] = faces[i].b;
        n_edges++;
        edge_a[n_edges] = faces[i].b;
        edge_b[n_edges] = faces[i].c;
        n_edges++;
        edge_a[n_edges] = faces[i].c;
        edge_b[n_edges] = faces[i].a;
        n_edges++;
    }
    for (i = 0; i < n_edges; i++)
    {
        for (j = i + 1; j < n_edges; j++)
        {
            if (edge_a[i] == edge_b[j] && edge_b[i] == edge_a[j])
            {
                edge_dead[i] = TRUE;
                edge_dead[j] = TRUE;
            }
        }
    }
    kept = g_new (SolidTri, capacity);
    written = 0;
    for (i = 0; i < *n_faces; i++)
    {
        if (!visible[i])
            kept[written++] = faces[i];
    }
    for (i = 0; i < n_edges; i++)
    {
        SolidTri added;
        gdouble forward;

        if (edge_dead[i])
            continue;
        if (written >= capacity)
        {
            g_free (visible);
            g_free (edge_a);
            g_free (edge_b);
            g_free (edge_dead);
            g_free (kept);
            return FALSE;
        }
        added.a = edge_a[i];
        added.b = edge_b[i];
        added.c = index;
        forward = orient_xyz (points + (gsize) added.a * 3,
                              points + (gsize) added.b * 3,
                              points + (gsize) added.c * 3,
                              interior);
        if (forward > 0.0)
        {
            guint swap;

            swap = added.b;
            added.b = added.c;
            added.c = swap;
        }
        kept[written++] = added;
    }
    memcpy (faces, kept, sizeof (SolidTri) * written);
    *n_faces = written;
    g_free (visible);
    g_free (edge_a);
    g_free (edge_b);
    g_free (edge_dead);
    g_free (kept);
    return TRUE;
}

static gboolean
build_hull (gdouble  *points,
            guint    *n_points,
            guint   **faces_out,
            guint    *n_faces_out,
            gdouble **planes_out,
            guint    *n_planes_out,
            GError  **error)
{
    SolidTri *faces;
    gdouble interior[3];
    gdouble epsilon, diagonal, min_c, max_c;
    guint n, i, j, seed[4], n_faces, face_i, plane_i;
    gdouble *planes;
    gboolean used[LRG_SOLID_MAX_CONVEX_POINTS];

    n = *n_points;
    if (n > LRG_SOLID_MAX_CONVEX_POINTS)
        n = LRG_SOLID_MAX_CONVEX_POINTS;
    diagonal = 0.0;
    for (i = 0; i < 3; i++)
    {
        min_c = points[i];
        max_c = points[i];
        for (j = 1; j < n; j++)
        {
            min_c = MIN (min_c, points[j * 3 + i]);
            max_c = MAX (max_c, points[j * 3 + i]);
        }
        diagonal += (max_c - min_c) * (max_c - min_c);
    }
    diagonal = sqrt (diagonal);
    epsilon = SOLID_ORIENT_FLOOR * (1.0 + diagonal);
    seed[0] = 0;
    seed[1] = 0;
    for (i = 1; i < n; i++)
    {
        gdouble delta[3];

        v_sub (points + (gsize) i * 3, points, delta);
        if (v_len (delta) > epsilon)
        {
            seed[1] = i;
            break;
        }
    }
    if (seed[1] == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex solid needs four non-coplanar points");
        return FALSE;
    }
    seed[2] = seed[1];
    for (i = 0; i < n; i++)
    {
        gdouble ab[3], ac[3], cross[3];

        v_sub (points + (gsize) seed[1] * 3, points, ab);
        v_sub (points + (gsize) i * 3, points, ac);
        v_cross (ab, ac, cross);
        if (v_len (cross) > epsilon * (1.0 + v_len (ab)))
        {
            seed[2] = i;
            break;
        }
    }
    if (seed[2] == seed[1])
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex solid needs four non-coplanar points");
        return FALSE;
    }
    seed[3] = seed[2];
    for (i = 0; i < n; i++)
    {
        if (fabs (orient_point (points, seed[0], seed[1], seed[2], i)) > epsilon)
        {
            seed[3] = i;
            break;
        }
    }
    if (seed[3] == seed[2])
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex solid needs four non-coplanar points");
        return FALSE;
    }
    for (i = 0; i < 3; i++)
        interior[i] = 0.25 * (points[seed[0] * 3 + i] + points[seed[1] * 3 + i] +
                              points[seed[2] * 3 + i] + points[seed[3] * 3 + i]);
    faces = g_new (SolidTri, LRG_SOLID_MAX_CONVEX_POINTS * 4);
    n_faces = 4;
    faces[0].a = seed[0];
    faces[0].b = seed[1];
    faces[0].c = seed[2];
    faces[1].a = seed[0];
    faces[1].b = seed[1];
    faces[1].c = seed[3];
    faces[2].a = seed[0];
    faces[2].b = seed[2];
    faces[2].c = seed[3];
    faces[3].a = seed[1];
    faces[3].b = seed[2];
    faces[3].c = seed[3];
    /* Outward means the centroid is behind every seed face. */
    for (i = 0; i < 4; i++)
    {
        if (orient_xyz (points + (gsize) faces[i].a * 3,
                        points + (gsize) faces[i].b * 3,
                        points + (gsize) faces[i].c * 3,
                        interior) > 0.0)
        {
            guint swap;

            swap = faces[i].b;
            faces[i].b = faces[i].c;
            faces[i].c = swap;
        }
    }
    memset (used, 0, sizeof used);
    used[seed[0]] = used[seed[1]] = used[seed[2]] = used[seed[3]] = TRUE;
    for (i = 0; i < n; i++)
    {
        if (used[i])
            continue;
        if (!hull_add_point (points, i, faces, &n_faces,
                             LRG_SOLID_MAX_CONVEX_POINTS * 4, interior, epsilon))
        {
            g_free (faces);
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "Convex hull exceeded its face budget");
            return FALSE;
        }
    }
    if (n_faces < 4)
    {
        g_free (faces);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex hull did not close");
        return FALSE;
    }
    faces = g_renew (SolidTri, faces, n_faces);
    planes = g_new (gdouble, n_faces * 4);
    plane_i = 0;
    for (face_i = 0; face_i < n_faces; face_i++)
    {
        gdouble ab[3], ac[3], normal[3];
        const gdouble *pa;

        pa = points + (gsize) faces[face_i].a * 3;
        v_sub (points + (gsize) faces[face_i].b * 3, pa, ab);
        v_sub (points + (gsize) faces[face_i].c * 3, pa, ac);
        v_cross (ab, ac, normal);
        if (!v_normalize (normal))
            continue;
        if (orient_xyz (pa, points + (gsize) faces[face_i].b * 3,
                        points + (gsize) faces[face_i].c * 3, interior) > 0.0)
        {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        planes[plane_i * 4 + 0] = normal[0];
        planes[plane_i * 4 + 1] = normal[1];
        planes[plane_i * 4 + 2] = normal[2];
        planes[plane_i * 4 + 3] = v_dot (normal, pa);
        plane_i++;
    }
    if (plane_i < 4)
    {
        g_free (faces);
        g_free (planes);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex hull did not produce outward planes");
        return FALSE;
    }
    *faces_out = g_new (guint, n_faces * 3);
    for (i = 0; i < n_faces; i++)
    {
        (*faces_out)[i * 3 + 0] = faces[i].a;
        (*faces_out)[i * 3 + 1] = faces[i].b;
        (*faces_out)[i * 3 + 2] = faces[i].c;
    }
    g_free (faces);
    *n_faces_out = n_faces;
    *planes_out = planes;
    *n_planes_out = plane_i;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Classification                                                           */
/* ------------------------------------------------------------------------ */

static void
box_local (const LrgSolidPrim *box, const gdouble *point, gdouble *local)
{
    gdouble rel[3];
    guint c;

    v_sub (point, box->center, rel);
    for (c = 0; c < 3; c++)
        local[c] = v_dot (box->axis[c], rel);
}

static LrgSolidClass
classify_sphere (const LrgSolidPrim *prim, const gdouble *point)
{
    gdouble rel[3];
    gdouble dist;

    v_sub (point, prim->center, rel);
    dist = v_len (rel);
    if (dist < prim->radius - SOLID_EPS)
        return LRG_SOLID_CLASS_INSIDE;
    if (dist <= prim->radius + SOLID_EPS)
        return LRG_SOLID_CLASS_TOUCHING;
    return LRG_SOLID_CLASS_OUTSIDE;
}

static LrgSolidClass
classify_box (const LrgSolidPrim *prim, const gdouble *point)
{
    gdouble local[3];
    gboolean inside, touching;
    guint c;

    box_local (prim, point, local);
    inside = TRUE;
    touching = TRUE;
    for (c = 0; c < 3; c++)
    {
        gdouble abs_c;

        abs_c = fabs (local[c]);
        if (abs_c >= prim->half[c] - SOLID_EPS)
            inside = FALSE;
        if (abs_c > prim->half[c] + SOLID_EPS)
            touching = FALSE;
    }
    if (inside)
        return LRG_SOLID_CLASS_INSIDE;
    if (touching)
        return LRG_SOLID_CLASS_TOUCHING;
    return LRG_SOLID_CLASS_OUTSIDE;
}

static void
sphyl_segment (const LrgSolidPrim *prim, gdouble *a, gdouble *b)
{
    v_mad (prim->center, prim->axis[1], -prim->half[1], a);
    v_mad (prim->center, prim->axis[1], prim->half[1], b);
}

static void
closest_segment (const gdouble *a, const gdouble *b, const gdouble *point, gdouble *out)
{
    gdouble ab[3], ap[3];
    gdouble denom, t;
    guint i;

    v_sub (b, a, ab);
    v_sub (point, a, ap);
    denom = v_dot (ab, ab);
    if (!(denom > 1e-20))
    {
        memcpy (out, a, sizeof (gdouble) * 3);
        return;
    }
    t = v_dot (ap, ab) / denom;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    for (i = 0; i < 3; i++)
        out[i] = a[i] + ab[i] * t;
}

/* Ericson, Real-Time Collision Detection: closest points of two segments. */
static void
closest_segments (const gdouble *p1, const gdouble *q1,
                  const gdouble *p2, const gdouble *q2,
                  gdouble *c1, gdouble *c2)
{
    gdouble d1[3], d2[3], r[3];
    gdouble a, b, c, e, f, denom, s, t;

    v_sub (q1, p1, d1);
    v_sub (q2, p2, d2);
    v_sub (p1, p2, r);
    a = v_dot (d1, d1);
    e = v_dot (d2, d2);
    f = v_dot (d2, r);
    s = 0.0;
    t = 0.0;
    if (a <= 1e-20 && e <= 1e-20)
    {
        memcpy (c1, p1, sizeof (gdouble) * 3);
        memcpy (c2, p2, sizeof (gdouble) * 3);
        return;
    }
    if (a <= 1e-20)
    {
        s = 0.0;
        t = f / e;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
    }
    else
    {
        c = v_dot (d1, r);
        if (e <= 1e-20)
        {
            t = 0.0;
            s = -c / a;
            if (s < 0.0)
                s = 0.0;
            if (s > 1.0)
                s = 1.0;
        }
        else
        {
            b = v_dot (d1, d2);
            denom = a * e - b * b;
            if (denom > 1e-20)
            {
                s = (b * f - c * e) / denom;
                if (s < 0.0)
                    s = 0.0;
                if (s > 1.0)
                    s = 1.0;
            }
            else
                s = 0.0;
            t = (b * s + f) / e;
            if (t < 0.0)
            {
                t = 0.0;
                s = -c / a;
                if (s < 0.0)
                    s = 0.0;
                if (s > 1.0)
                    s = 1.0;
            }
            else if (t > 1.0)
            {
                t = 1.0;
                s = (b - c) / a;
                if (s < 0.0)
                    s = 0.0;
                if (s > 1.0)
                    s = 1.0;
            }
        }
    }
    v_mad (p1, d1, s, c1);
    v_mad (p2, d2, t, c2);
}

static LrgSolidClass
classify_sphyl (const LrgSolidPrim *prim, const gdouble *point)
{
    gdouble a[3], b[3], closest[3];
    LrgSolidPrim cap;

    sphyl_segment (prim, a, b);
    closest_segment (a, b, point, closest);
    memset (&cap, 0, sizeof cap);
    memcpy (cap.center, closest, sizeof closest);
    cap.radius = prim->radius;
    return classify_sphere (&cap, point);
}

static gdouble
plane_signed (const gdouble *plane, const gdouble *point)
{
    return plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] - plane[3];
}

static LrgSolidClass
classify_convex (const LrgSolidPrim *prim, const gdouble *point)
{
    gdouble worst;
    guint i;

    worst = -G_MAXDOUBLE;
    for (i = 0; i < prim->n_planes; i++)
        worst = MAX (worst, plane_signed (prim->planes + (gsize) i * 4, point));
    if (worst < -SOLID_EPS)
        return LRG_SOLID_CLASS_INSIDE;
    if (worst <= SOLID_EPS)
        return LRG_SOLID_CLASS_TOUCHING;
    return LRG_SOLID_CLASS_OUTSIDE;
}

static LrgSolidClass
classify_one (const LrgSolidPrim *prim, const gdouble *point)
{
    switch (prim->kind)
    {
    case LRG_SOLID_PRIM_SPHERE: return classify_sphere (prim, point);
    case LRG_SOLID_PRIM_BOX: return classify_box (prim, point);
    case LRG_SOLID_PRIM_SPHYL: return classify_sphyl (prim, point);
    case LRG_SOLID_PRIM_CONVEX: return classify_convex (prim, point);
    default: return LRG_SOLID_CLASS_OUTSIDE;
    }
}

gboolean
lrg_solid_prims_classify (const LrgSolidPrim *prims,
                          guint               n_prims,
                          const gdouble       point[3],
                          LrgSolidClass      *result)
{
    gboolean touching;
    guint i;

    touching = FALSE;
    for (i = 0; i < n_prims; i++)
    {
        LrgSolidClass one;

        one = classify_one (&prims[i], point);
        if (one == LRG_SOLID_CLASS_INSIDE)
        {
            *result = LRG_SOLID_CLASS_INSIDE;
            return TRUE;
        }
        if (one == LRG_SOLID_CLASS_TOUCHING)
            touching = TRUE;
    }
    *result = touching ? LRG_SOLID_CLASS_TOUCHING : LRG_SOLID_CLASS_OUTSIDE;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Overlap                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gboolean overlapping;
    gdouble  depth;
    gdouble  normal[3];
    gdouble  point[3];
} RawContact;

static void
raw_from_spheres (const gdouble *c1, gdouble r1, const gdouble *c2, gdouble r2, RawContact *raw)
{
    gdouble delta[3];
    gdouble dist, depth;

    v_sub (c2, c1, delta);
    dist = v_len (delta);
    depth = r1 + r2 - dist;
    raw->overlapping = depth >= -SOLID_EPS;
    raw->depth = depth > 0.0 ? depth : 0.0;
    if (dist > 1e-12)
    {
        raw->normal[0] = delta[0] / dist;
        raw->normal[1] = delta[1] / dist;
        raw->normal[2] = delta[2] / dist;
    }
    else
    {
        raw->normal[0] = 0.0;
        raw->normal[1] = 1.0;
        raw->normal[2] = 0.0;
    }
    v_mad (c1, raw->normal, r1, raw->point);
}

static gboolean
sphere_box_mtv (const gdouble *center, gdouble radius, const LrgSolidPrim *box, RawContact *raw)
{
    gdouble local[3], closest_local[3], closest[3], delta[3];
    gdouble dist, best, sign;
    guint c, axis;

    box_local (box, center, local);
    dist = 0.0;
    for (c = 0; c < 3; c++)
    {
        closest_local[c] = local[c];
        if (closest_local[c] < -box->half[c])
            closest_local[c] = -box->half[c];
        if (closest_local[c] > box->half[c])
            closest_local[c] = box->half[c];
    }
    memcpy (closest, box->center, sizeof closest);
    for (c = 0; c < 3; c++)
        v_mad (closest, box->axis[c], closest_local[c], closest);
    v_sub (center, closest, delta);
    dist = v_len (delta);
    if (dist > radius + SOLID_EPS)
    {
        raw->overlapping = FALSE;
        raw->depth = 0.0;
        return TRUE;
    }
    raw->overlapping = TRUE;
    if (dist > 1e-10)
    {
        raw->depth = radius - dist;
        if (raw->depth < 0.0)
            raw->depth = 0.0;
        raw->normal[0] = delta[0] / dist;
        raw->normal[1] = delta[1] / dist;
        raw->normal[2] = delta[2] / dist;
        memcpy (raw->point, closest, sizeof closest);
        return TRUE;
    }
    best = G_MAXDOUBLE;
    axis = 0;
    sign = 1.0;
    for (c = 0; c < 3; c++)
    {
        gdouble positive, negative;

        positive = box->half[c] - local[c];
        negative = box->half[c] + local[c];
        if (positive < best)
        {
            best = positive;
            axis = c;
            sign = 1.0;
        }
        if (negative < best)
        {
            best = negative;
            axis = c;
            sign = -1.0;
        }
    }
    raw->depth = best + radius;
    raw->normal[0] = box->axis[axis][0] * sign;
    raw->normal[1] = box->axis[axis][1] * sign;
    raw->normal[2] = box->axis[axis][2] * sign;
    v_mad (center, raw->normal, best, raw->point);
    return TRUE;
}

static void
flip_raw (RawContact *raw)
{
    raw->normal[0] = -raw->normal[0];
    raw->normal[1] = -raw->normal[1];
    raw->normal[2] = -raw->normal[2];
}

/* After a flip, the witness has to sit on the first solid. The helper
 * that produced the contact put it on the other shape. */
static void
point_on_sphere (const gdouble *center, gdouble radius, RawContact *raw)
{
    v_mad (center, raw->normal, radius, raw->point);
}

static gboolean
point_in_tri (const gdouble *point, const gdouble *a, const gdouble *b, const gdouble *c)
{
    gdouble v0[3], v1[3], v2[3];
    gdouble dot00, dot01, dot02, dot11, dot12, denom, u, v;

    v_sub (b, a, v0);
    v_sub (c, a, v1);
    v_sub (point, a, v2);
    dot00 = v_dot (v0, v0);
    dot01 = v_dot (v0, v1);
    dot02 = v_dot (v0, v2);
    dot11 = v_dot (v1, v1);
    dot12 = v_dot (v1, v2);
    denom = dot00 * dot11 - dot01 * dot01;
    if (fabs (denom) < 1e-20)
        return FALSE;
    u = (dot11 * dot02 - dot01 * dot12) / denom;
    v = (dot00 * dot12 - dot01 * dot02) / denom;
    return u >= -1e-6 && v >= -1e-6 && (u + v) <= 1.0 + 1e-6;
}

static gdouble
closest_on_convex (const LrgSolidPrim *prim, const gdouble *point, gdouble *closest, gboolean *inside)
{
    gdouble best, signed_worst, dist;
    guint i, e;

    signed_worst = -G_MAXDOUBLE;
    for (i = 0; i < prim->n_planes; i++)
        signed_worst = MAX (signed_worst, plane_signed (prim->planes + (gsize) i * 4, point));
    *inside = signed_worst <= SOLID_EPS;
    best = G_MAXDOUBLE;
    memcpy (closest, prim->points, sizeof (gdouble) * 3);
    for (i = 0; i < prim->n_points; i++)
    {
        gdouble rel[3];

        v_sub (point, prim->points + (gsize) i * 3, rel);
        dist = v_len (rel);
        if (dist < best)
        {
            best = dist;
            memcpy (closest, prim->points + (gsize) i * 3, sizeof (gdouble) * 3);
        }
    }
    for (i = 0; i < prim->n_faces; i++)
    {
        const gdouble *a, *b, *c;
        gdouble ab[3], ac[3], normal[3], rel[3], projected[3];
        gdouble denom, t;

        a = prim->points + (gsize) prim->faces[i * 3 + 0] * 3;
        b = prim->points + (gsize) prim->faces[i * 3 + 1] * 3;
        c = prim->points + (gsize) prim->faces[i * 3 + 2] * 3;
        v_sub (b, a, ab);
        v_sub (c, a, ac);
        v_cross (ab, ac, normal);
        denom = v_dot (normal, normal);
        if (!(denom > 1e-20))
            continue;
        v_sub (point, a, rel);
        t = v_dot (rel, normal) / denom;
        v_mad (point, normal, -t, projected);
        if (!point_in_tri (projected, a, b, c))
            continue;
        dist = fabs (t) * sqrt (denom);
        if (dist < best)
        {
            best = dist;
            memcpy (closest, projected, sizeof projected);
        }
        for (e = 0; e < 3; e++)
        {
            const gdouble *s, *t_point;
            gdouble end[3];

            s = (e == 0) ? a : (e == 1) ? b : c;
            t_point = (e == 0) ? b : (e == 1) ? c : a;
            closest_segment (s, t_point, point, end);
            v_sub (point, end, rel);
            dist = v_len (rel);
            if (dist < best)
            {
                best = dist;
                memcpy (closest, end, sizeof end);
            }
        }
    }
    if (*inside)
        return -signed_worst;
    return best;
}

static gboolean
overlap_sphere_convex (const LrgSolidPrim *sphere, const LrgSolidPrim *convex, RawContact *raw)
{
    gdouble closest[3], delta[3];
    gboolean inside;
    gdouble dist, clearance, best;
    guint i, which;

    dist = closest_on_convex (convex, sphere->center, closest, &inside);
    if (inside || dist <= sphere->radius + SOLID_EPS)
    {
        raw->overlapping = TRUE;
        if (!inside && dist > 1e-10)
        {
            v_sub (sphere->center, closest, delta);
            raw->depth = sphere->radius - dist;
            if (raw->depth < 0.0)
                raw->depth = 0.0;
            if (!v_normalize (delta))
            {
                delta[0] = 0.0;
                delta[1] = 1.0;
                delta[2] = 0.0;
            }
            memcpy (raw->normal, delta, sizeof delta);
            memcpy (raw->point, closest, sizeof closest);
            return TRUE;
        }
        best = G_MAXDOUBLE;
        which = 0;
        for (i = 0; i < convex->n_planes; i++)
        {
            clearance = -plane_signed (convex->planes + (gsize) i * 4, sphere->center);
            if (clearance < best)
            {
                best = clearance;
                which = i;
            }
        }
        raw->normal[0] = convex->planes[which * 4 + 0];
        raw->normal[1] = convex->planes[which * 4 + 1];
        raw->normal[2] = convex->planes[which * 4 + 2];
        raw->depth = best + sphere->radius;
        if (raw->depth < 0.0)
            raw->depth = 0.0;
        v_mad (sphere->center, raw->normal, best, raw->point);
        return TRUE;
    }
    raw->overlapping = FALSE;
    raw->depth = 0.0;
    return TRUE;
}

static void
collect_vertices (const LrgSolidPrim *prim, gdouble *out, guint *n_out)
{
    guint corner, c, i;

    if (prim->kind == LRG_SOLID_PRIM_BOX)
    {
        for (corner = 0; corner < 8; corner++)
        {
            for (i = 0; i < 3; i++)
                out[corner * 3 + i] = prim->center[i];
            for (c = 0; c < 3; c++)
            {
                gdouble sign;

                sign = (corner & (1u << c)) ? prim->half[c] : -prim->half[c];
                out[corner * 3 + 0] += sign * prim->axis[c][0];
                out[corner * 3 + 1] += sign * prim->axis[c][1];
                out[corner * 3 + 2] += sign * prim->axis[c][2];
            }
        }
        *n_out = 8;
        return;
    }
    memcpy (out, prim->points, sizeof (gdouble) * prim->n_points * 3);
    *n_out = prim->n_points;
}

static void
project_span (const gdouble *points, guint n, const gdouble *axis, gdouble *min_out, gdouble *max_out)
{
    guint i;
    gdouble value;

    *min_out = v_dot (points, axis);
    *max_out = *min_out;
    for (i = 1; i < n; i++)
    {
        value = v_dot (points + (gsize) i * 3, axis);
        if (value < *min_out)
            *min_out = value;
        if (value > *max_out)
            *max_out = value;
    }
}

/* A 32-point triangular hull has at most 3*32-6 = 90 unique edges. */
#define SAT_MAX_EDGES (128)

static gboolean
sat_push_edge (guint edges[][2], guint *n, guint i0, guint i1)
{
    guint i, lo, hi;

    lo = MIN (i0, i1);
    hi = MAX (i0, i1);
    if (lo == hi)
        return TRUE;
    for (i = 0; i < *n; i++)
    {
        if (edges[i][0] == lo && edges[i][1] == hi)
            return TRUE;
    }
    if (*n >= SAT_MAX_EDGES)
        return FALSE;
    edges[*n][0] = lo;
    edges[*n][1] = hi;
    (*n)++;
    return TRUE;
}

static gboolean
sat_collect_edges (const LrgSolidPrim *prim, guint edges[][2], guint *n)
{
    guint face, slot;

    *n = 0;
    if (prim->kind == LRG_SOLID_PRIM_BOX)
    {
        for (slot = 0; slot < 3; slot++)
        {
            edges[*n][0] = slot;
            edges[*n][1] = slot;
            (*n)++;
        }
        return TRUE;
    }
    for (face = 0; face < prim->n_faces; face++)
    {
        for (slot = 0; slot < 3; slot++)
        {
            if (!sat_push_edge (edges, n,
                                prim->faces[face * 3 + slot],
                                prim->faces[face * 3 + (slot + 1) % 3]))
                return FALSE;
        }
    }
    return TRUE;
}

static void
sat_edge_dir (const LrgSolidPrim *prim, const guint *edge, gdouble *out)
{
    if (prim->kind == LRG_SOLID_PRIM_BOX)
        memcpy (out, prim->axis[edge[0]], sizeof (gdouble) * 3);
    else
        v_sub (prim->points + (gsize) edge[1] * 3,
               prim->points + (gsize) edge[0] * 3, out);
}

/* 1 = this axis separates. 0 = overlapping on this axis, depth recorded. */
static gint
sat_axis (const gdouble *va, guint na, const gdouble *vb, guint nb,
          const gdouble *axis_in, gdouble *best_depth, gdouble *best_axis)
{
    gdouble axis[3];
    gdouble min_a, max_a, min_b, max_b, overlap;

    memcpy (axis, axis_in, sizeof axis);
    if (!v_normalize (axis))
        return 0;
    project_span (va, na, axis, &min_a, &max_a);
    project_span (vb, nb, axis, &min_b, &max_b);
    if (max_a < min_b - SOLID_EPS || max_b < min_a - SOLID_EPS)
        return 1;
    overlap = MIN (max_a, max_b) - MAX (min_a, min_b);
    if (overlap < *best_depth)
    {
        *best_depth = overlap;
        memcpy (best_axis, axis, sizeof axis);
    }
    return 0;
}

static gboolean
sat_pair (const LrgSolidPrim *a, const LrgSolidPrim *b, RawContact *raw, GError **error)
{
    gdouble va[32 * 3], vb[32 * 3];
    gdouble best_depth, best_axis[3], center_a[3], center_b[3], delta[3];
    guint na, nb, i, c, face, edge;
    guint edges_a[SAT_MAX_EDGES][2], edges_b[SAT_MAX_EDGES][2], n_ea, n_eb;
    const LrgSolidPrim *side[2];
    guint si;

    if ((a->kind != LRG_SOLID_PRIM_BOX && a->kind != LRG_SOLID_PRIM_CONVEX) ||
        (b->kind != LRG_SOLID_PRIM_BOX && b->kind != LRG_SOLID_PRIM_CONVEX))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "Solid pair %s/%s is not supported", prim_name (a->kind), prim_name (b->kind));
        return FALSE;
    }
    collect_vertices (a, va, &na);
    collect_vertices (b, vb, &nb);
    if (!sat_collect_edges (a, edges_a, &n_ea) || !sat_collect_edges (b, edges_b, &n_eb))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Solid convex edge count exceeds the SAT budget");
        return FALSE;
    }
    best_depth = G_MAXDOUBLE;
    best_axis[0] = 0.0;
    best_axis[1] = 1.0;
    best_axis[2] = 0.0;
    side[0] = a;
    side[1] = b;
    for (si = 0; si < 2; si++)
    {
        if (side[si]->kind == LRG_SOLID_PRIM_BOX)
        {
            for (c = 0; c < 3; c++)
            {
                if (sat_axis (va, na, vb, nb, side[si]->axis[c], &best_depth, best_axis) == 1)
                {
                    raw->overlapping = FALSE;
                    raw->depth = 0.0;
                    return TRUE;
                }
            }
        }
        else
        {
            for (face = 0; face < side[si]->n_planes; face++)
            {
                if (sat_axis (va, na, vb, nb, side[si]->planes + (gsize) face * 4,
                              &best_depth, best_axis) == 1)
                {
                    raw->overlapping = FALSE;
                    raw->depth = 0.0;
                    return TRUE;
                }
            }
        }
    }
    for (i = 0; i < n_ea; i++)
    {
        for (edge = 0; edge < n_eb; edge++)
        {
            gdouble da[3], db[3], cross[3];

            sat_edge_dir (a, edges_a[i], da);
            sat_edge_dir (b, edges_b[edge], db);
            v_cross (da, db, cross);
            if (v_len (cross) < 1e-8)
                continue;
            if (sat_axis (va, na, vb, nb, cross, &best_depth, best_axis) == 1)
            {
                raw->overlapping = FALSE;
                raw->depth = 0.0;
                return TRUE;
            }
        }
    }
    memset (center_a, 0, sizeof center_a);
    memset (center_b, 0, sizeof center_b);
    for (i = 0; i < na; i++)
        for (c = 0; c < 3; c++)
            center_a[c] += va[i * 3 + c] / (gdouble) na;
    for (i = 0; i < nb; i++)
        for (c = 0; c < 3; c++)
            center_b[c] += vb[i * 3 + c] / (gdouble) nb;
    v_sub (center_b, center_a, delta);
    if (v_dot (best_axis, delta) < 0.0)
    {
        best_axis[0] = -best_axis[0];
        best_axis[1] = -best_axis[1];
        best_axis[2] = -best_axis[2];
    }
    raw->overlapping = TRUE;
    raw->depth = best_depth > 0.0 ? best_depth : 0.0;
    memcpy (raw->normal, best_axis, sizeof best_axis);
    memset (raw->point, 0, sizeof raw->point);
    {
        gdouble support, weight;

        support = -G_MAXDOUBLE;
        for (i = 0; i < na; i++)
            support = MAX (support, v_dot (va + (gsize) i * 3, best_axis));
        weight = 0.0;
        for (i = 0; i < na; i++)
        {
            if (v_dot (va + (gsize) i * 3, best_axis) >= support - 1e-4)
            {
                for (c = 0; c < 3; c++)
                    raw->point[c] += va[i * 3 + c];
                weight += 1.0;
            }
        }
        if (weight > 0.0)
            for (c = 0; c < 3; c++)
                raw->point[c] /= weight;
    }
    return TRUE;
}

static gboolean
overlap_one (const LrgSolidPrim *a, const LrgSolidPrim *b, RawContact *raw, GError **error)
{
    LrgSolidPrimKind ka, kb;

    ka = a->kind;
    kb = b->kind;
    if (ka == LRG_SOLID_PRIM_SPHERE && kb == LRG_SOLID_PRIM_SPHERE)
    {
        raw_from_spheres (a->center, a->radius, b->center, b->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_SPHERE && kb == LRG_SOLID_PRIM_BOX)
    {
        sphere_box_mtv (a->center, a->radius, b, raw);
        /* sphere_box_mtv moves the sphere out of the box and records a
         * point on the box. The first solid is the sphere, so the normal
         * reverses and the witness moves onto the sphere. */
        flip_raw (raw);
        point_on_sphere (a->center, a->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_BOX && kb == LRG_SOLID_PRIM_SPHERE)
    {
        sphere_box_mtv (b->center, b->radius, a, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_SPHERE && kb == LRG_SOLID_PRIM_SPHYL)
    {
        gdouble seg_a[3], seg_b[3], closest[3];

        sphyl_segment (b, seg_a, seg_b);
        closest_segment (seg_a, seg_b, a->center, closest);
        raw_from_spheres (a->center, a->radius, closest, b->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_SPHYL && kb == LRG_SOLID_PRIM_SPHERE)
    {
        gdouble seg_a[3], seg_b[3], closest[3];

        sphyl_segment (a, seg_a, seg_b);
        closest_segment (seg_a, seg_b, b->center, closest);
        raw_from_spheres (closest, a->radius, b->center, b->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_SPHYL && kb == LRG_SOLID_PRIM_SPHYL)
    {
        gdouble a0[3], a1[3], b0[3], b1[3], ca[3], cb[3];

        sphyl_segment (a, a0, a1);
        sphyl_segment (b, b0, b1);
        closest_segments (a0, a1, b0, b1, ca, cb);
        raw_from_spheres (ca, a->radius, cb, b->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_SPHERE && kb == LRG_SOLID_PRIM_CONVEX)
    {
        overlap_sphere_convex (a, b, raw);
        flip_raw (raw);
        point_on_sphere (a->center, a->radius, raw);
        return TRUE;
    }
    if (ka == LRG_SOLID_PRIM_CONVEX && kb == LRG_SOLID_PRIM_SPHERE)
        return overlap_sphere_convex (b, a, raw);
    if ((ka == LRG_SOLID_PRIM_BOX || ka == LRG_SOLID_PRIM_CONVEX) &&
        (kb == LRG_SOLID_PRIM_BOX || kb == LRG_SOLID_PRIM_CONVEX))
        return sat_pair (a, b, raw, error);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "Solid pair %s/%s is not supported", prim_name (ka), prim_name (kb));
    return FALSE;
}

static gboolean
better_contact (gdouble depth, guint ia, guint ib, gdouble best_depth, guint best_a, guint best_b, gboolean have)
{
    if (!have)
        return TRUE;
    if (depth > best_depth + 1e-8)
        return TRUE;
    if (depth < best_depth - 1e-8)
        return FALSE;
    if (ia < best_a)
        return TRUE;
    if (ia == best_a && ib < best_b)
        return TRUE;
    return FALSE;
}

gboolean
lrg_solid_prims_overlap (const LrgSolidPrim *a,
                         guint               n_a,
                         const LrgSolidPrim *b,
                         guint               n_b,
                         LrgSolidContact    *contact,
                         gboolean           *overlapping,
                         GError            **error)
{
    RawContact best, one;
    gboolean have;
    guint ia, ib, best_a, best_b;

    have = FALSE;
    best_a = 0;
    best_b = 0;
    memset (&best, 0, sizeof best);
    for (ia = 0; ia < n_a; ia++)
    {
        for (ib = 0; ib < n_b; ib++)
        {
            if (!overlap_one (&a[ia], &b[ib], &one, error))
                return FALSE;
            if (!one.overlapping)
                continue;
            if (better_contact (one.depth, ia, ib, best.depth, best_a, best_b, have))
            {
                best = one;
                best_a = ia;
                best_b = ib;
                have = TRUE;
            }
        }
    }
    *overlapping = have;
    if (have && contact != NULL)
    {
        float3 (best.point, contact->point);
        float3 (best.normal, contact->normal);
        contact->depth = (gfloat) best.depth;
        contact->primitive_a = best_a;
        contact->primitive_b = best_b;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Containment                                                              */
/* ------------------------------------------------------------------------ */

static gboolean
point_contained (const LrgSolidPrim *outer, const gdouble *point)
{
    LrgSolidClass klass;

    klass = classify_one (outer, point);
    return klass != LRG_SOLID_CLASS_OUTSIDE;
}

static gboolean
sphere_in_prim (const LrgSolidPrim *sphere, const LrgSolidPrim *outer)
{
    gdouble local[3];
    guint i;

    if (outer->kind == LRG_SOLID_PRIM_SPHERE)
    {
        gdouble rel[3];

        v_sub (sphere->center, outer->center, rel);
        return v_len (rel) + sphere->radius <= outer->radius + SOLID_EPS;
    }
    if (outer->kind == LRG_SOLID_PRIM_BOX)
    {
        box_local (outer, sphere->center, local);
        for (i = 0; i < 3; i++)
            if (fabs (local[i]) + sphere->radius > outer->half[i] + SOLID_EPS)
                return FALSE;
        return TRUE;
    }
    if (outer->kind == LRG_SOLID_PRIM_SPHYL)
    {
        gdouble a[3], b[3], closest[3], rel[3];

        sphyl_segment (outer, a, b);
        closest_segment (a, b, sphere->center, closest);
        v_sub (sphere->center, closest, rel);
        return v_len (rel) + sphere->radius <= outer->radius + SOLID_EPS;
    }
    if (outer->kind == LRG_SOLID_PRIM_CONVEX)
    {
        for (i = 0; i < outer->n_planes; i++)
            if (plane_signed (outer->planes + (gsize) i * 4, sphere->center) >
                -sphere->radius + SOLID_EPS)
                return FALSE;
        return TRUE;
    }
    return FALSE;
}

static gboolean
poly_in_prim (const gdouble *points, guint n_points, const LrgSolidPrim *outer)
{
    guint i;

    for (i = 0; i < n_points; i++)
        if (!point_contained (outer, points + (gsize) i * 3))
            return FALSE;
    return TRUE;
}

static gboolean
prim_contained (const LrgSolidPrim *inner, const LrgSolidPrim *outer, GError **error)
{
    gdouble corners[8 * 3];
    guint n_corners;
    gdouble a[3], b[3];
    LrgSolidPrim cap;

    if (inner->kind == LRG_SOLID_PRIM_SPHERE)
        return sphere_in_prim (inner, outer);
    if (inner->kind == LRG_SOLID_PRIM_BOX)
    {
        collect_vertices (inner, corners, &n_corners);
        return poly_in_prim (corners, n_corners, outer);
    }
    if (inner->kind == LRG_SOLID_PRIM_CONVEX)
        return poly_in_prim (inner->points, inner->n_points, outer);
    if (inner->kind == LRG_SOLID_PRIM_SPHYL)
    {
        sphyl_segment (inner, a, b);
        memset (&cap, 0, sizeof cap);
        cap.kind = LRG_SOLID_PRIM_SPHERE;
        cap.radius = inner->radius;
        memcpy (cap.center, a, sizeof a);
        if (!sphere_in_prim (&cap, outer))
            return FALSE;
        memcpy (cap.center, b, sizeof b);
        return sphere_in_prim (&cap, outer);
    }
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Solid containment pair is not supported");
    return FALSE;
}

gboolean
lrg_solid_prims_contains (const LrgSolidPrim *outer,
                          guint               n_outer,
                          const LrgSolidPrim *inner,
                          guint               n_inner,
                          gboolean           *contained,
                          GError            **error)
{
    guint i;

    if (n_outer != 1)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Containment in an aggregate union is not supported");
        return FALSE;
    }
    for (i = 0; i < n_inner; i++)
    {
        if (!prim_contained (&inner[i], &outer[0], error))
        {
            if (error != NULL && *error != NULL)
                return FALSE;
            *contained = FALSE;
            return TRUE;
        }
    }
    *contained = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Sphere sweep                                                             */
/* ------------------------------------------------------------------------ */

static gboolean
ray_sphere_param (const gdouble *origin, const gdouble *delta, const gdouble *center,
                  gdouble radius, gdouble *t_out)
{
    gdouble offset[3];
    gdouble A, B, C, disc, root, t0, t1;

    v_sub (origin, center, offset);
    A = v_dot (delta, delta);
    B = 2.0 * v_dot (offset, delta);
    C = v_dot (offset, offset) - radius * radius;
    if (C <= SOLID_EPS)
    {
        *t_out = 0.0;
        return TRUE;
    }
    if (!(A > 1e-20))
        return FALSE;
    disc = B * B - 4.0 * A * C;
    if (disc < 0.0)
        return FALSE;
    root = sqrt (disc);
    t0 = (-B - root) / (2.0 * A);
    t1 = (-B + root) / (2.0 * A);
    if (t0 >= -SOLID_EPS && t0 <= 1.0)
    {
        *t_out = t0 < 0.0 ? 0.0 : t0;
        return TRUE;
    }
    if (t1 >= -SOLID_EPS && t1 <= 1.0)
    {
        *t_out = t1 < 0.0 ? 0.0 : t1;
        return TRUE;
    }
    return FALSE;
}

static gboolean
sweep_sphere_sphere (const gdouble *center, gdouble radius, const gdouble *delta,
                     const LrgSolidPrim *sphere, gdouble *t_out, gdouble *normal, gdouble *point)
{
    gdouble at[3], rel[3];
    gdouble combined;

    combined = radius + sphere->radius;
    if (!ray_sphere_param (center, delta, sphere->center, combined, t_out))
        return FALSE;
    v_mad (center, delta, *t_out, at);
    v_sub (at, sphere->center, rel);
    if (!v_normalize (rel))
    {
        rel[0] = 0.0;
        rel[1] = 1.0;
        rel[2] = 0.0;
    }
    memcpy (normal, rel, sizeof (gdouble) * 3);
    v_mad (sphere->center, rel, sphere->radius, point);
    return TRUE;
}

static gboolean
sweep_sphere_sphyl (const gdouble *center, gdouble radius, const gdouble *delta,
                    const LrgSolidPrim *sphyl, gdouble *t_out, gdouble *normal, gdouble *point);

static gboolean
sweep_sphere_box (const gdouble *center, gdouble radius, const gdouble *delta,
                  const LrgSolidPrim *box, gdouble *t_out, gdouble *normal, gdouble *point)
{
    RawContact raw;
    gdouble best_t, face_n[3], origin[3];
    guint c, corner, edge;
    gboolean found;

    sphere_box_mtv (center, radius, box, &raw);
    if (raw.overlapping)
    {
        *t_out = 0.0;
        memcpy (normal, raw.normal, sizeof (gdouble) * 3);
        memcpy (point, raw.point, sizeof (gdouble) * 3);
        return TRUE;
    }
    found = FALSE;
    best_t = 2.0;
    for (c = 0; c < 3; c++)
    {
        gdouble sign, plane_d, speed, t, local_hit[3], world[3];
        guint k;

        for (sign = -1.0; sign <= 1.0; sign += 2.0)
        {
            memcpy (face_n, box->axis[c], sizeof face_n);
            if (sign < 0.0)
            {
                face_n[0] = -face_n[0];
                face_n[1] = -face_n[1];
                face_n[2] = -face_n[2];
            }
            v_mad (box->center, face_n, box->half[c] + radius, origin);
            plane_d = v_dot (face_n, origin);
            speed = v_dot (face_n, delta);
            if (!(fabs (speed) > 1e-12))
                continue;
            t = (plane_d - v_dot (face_n, center)) / speed;
            if (t < -SOLID_EPS || t > 1.0)
                continue;
            if (t < 0.0)
                t = 0.0;
            v_mad (center, delta, t, world);
            /* Pull back onto the flat face, then test the unexpanded rectangle. */
            v_mad (world, face_n, -radius, world);
            box_local (box, world, local_hit);
            if (fabs (local_hit[c] - sign * box->half[c]) > 1e-3)
                continue;
            for (k = 0; k < 3; k++)
            {
                if (k == c)
                    continue;
                if (fabs (local_hit[k]) > box->half[k] + 1e-4)
                    break;
            }
            if (k < 3)
                continue;
            if (t < best_t)
            {
                best_t = t;
                found = TRUE;
                /* face_n already points out of the box, which is the
                 * direction that pushes the moving sphere away. */
                normal[0] = face_n[0];
                normal[1] = face_n[1];
                normal[2] = face_n[2];
                v_mad (box->center, face_n, box->half[c], point);
                for (k = 0; k < 3; k++)
                {
                    if (k == c)
                        continue;
                    point[0] += box->axis[k][0] * local_hit[k];
                    point[1] += box->axis[k][1] * local_hit[k];
                    point[2] += box->axis[k][2] * local_hit[k];
                }
            }
        }
    }
    for (edge = 0; edge < 12; edge++)
    {
        guint axis, a_bit, b_bit;
        gdouble p0[3], p1[3], direction[3];
        gdouble t_hit, nrm[3], pt[3];
        LrgSolidPrim cap;
        gint sign_a, sign_b;

        axis = edge / 4;
        a_bit = (axis + 1) % 3;
        b_bit = (axis + 2) % 3;
        sign_a = (edge % 4) & 1 ? 1 : -1;
        sign_b = (edge % 4) & 2 ? 1 : -1;
        memcpy (p0, box->center, sizeof p0);
        v_mad (p0, box->axis[a_bit], (gdouble) sign_a * box->half[a_bit], p0);
        v_mad (p0, box->axis[b_bit], (gdouble) sign_b * box->half[b_bit], p0);
        v_mad (p0, box->axis[axis], -box->half[axis], p0);
        v_mad (p0, box->axis[axis], box->half[axis] * 2.0, p1);
        memset (&cap, 0, sizeof cap);
        cap.kind = LRG_SOLID_PRIM_SPHYL;
        cap.radius = 0.0;
        axis_identity (cap.axis);
        cap.center[0] = (p0[0] + p1[0]) * 0.5;
        cap.center[1] = (p0[1] + p1[1]) * 0.5;
        cap.center[2] = (p0[2] + p1[2]) * 0.5;
        v_sub (p1, p0, direction);
        cap.half[1] = 0.5 * v_len (direction);
        if (!v_normalize (direction))
            continue;
        memcpy (cap.axis[1], direction, sizeof direction);
        if (sweep_sphere_sphyl (center, radius, delta, &cap, &t_hit, nrm, &pt[0]) &&
            (!found || t_hit < best_t))
        {
            found = TRUE;
            best_t = t_hit;
            memcpy (normal, nrm, sizeof nrm);
            memcpy (point, pt, sizeof pt);
        }
    }
    for (corner = 0; corner < 8; corner++)
    {
        gdouble vertex[3], t_hit, nrm[3], pt[3];
        LrgSolidPrim sphere;
        guint i;

        for (i = 0; i < 3; i++)
            vertex[i] = box->center[i];
        for (c = 0; c < 3; c++)
        {
            gdouble sign;

            sign = (corner & (1u << c)) ? box->half[c] : -box->half[c];
            vertex[0] += sign * box->axis[c][0];
            vertex[1] += sign * box->axis[c][1];
            vertex[2] += sign * box->axis[c][2];
        }
        memset (&sphere, 0, sizeof sphere);
        sphere.kind = LRG_SOLID_PRIM_SPHERE;
        sphere.radius = 0.0;
        memcpy (sphere.center, vertex, sizeof vertex);
        if (sweep_sphere_sphere (center, radius, delta, &sphere, &t_hit, nrm, pt) &&
            (!found || t_hit < best_t))
        {
            found = TRUE;
            best_t = t_hit;
            memcpy (normal, nrm, sizeof nrm);
            memcpy (point, vertex, sizeof vertex);
        }
    }
    if (found)
        *t_out = best_t;
    return found;
}

static gboolean
sweep_sphere_sphyl (const gdouble *center, gdouble radius, const gdouble *delta,
                    const LrgSolidPrim *sphyl, gdouble *t_out, gdouble *normal, gdouble *point)
{
    gdouble a[3], b[3];
    LrgSolidPrim cap;

    sphyl_segment (sphyl, a, b);
    memset (&cap, 0, sizeof cap);
    cap.kind = LRG_SOLID_PRIM_SPHERE;
    cap.radius = sphyl->radius;
    memcpy (cap.center, a, sizeof a);
    if (sweep_sphere_sphere (center, radius, delta, &cap, t_out, normal, point))
    {
        gdouble t_b, n_b[3], p_b[3];

        memcpy (cap.center, b, sizeof b);
        if (sweep_sphere_sphere (center, radius, delta, &cap, &t_b, n_b, p_b) && t_b < *t_out)
        {
            *t_out = t_b;
            memcpy (normal, n_b, sizeof n_b);
            memcpy (point, p_b, sizeof p_b);
        }
    }
    else
    {
        memcpy (cap.center, b, sizeof b);
        if (!sweep_sphere_sphere (center, radius, delta, &cap, t_out, normal, point))
        {
            /* Fall through to the cylinder. A miss of both spheres can still
             * hit the side. */
            *t_out = 2.0;
        }
    }
    {
        gdouble ba[3], oa[3], rd[3];
        gdouble baba, bard, baoa, rdoa, oaoa, A, B, C, h, t, y;
        gdouble combined;

        v_sub (b, a, ba);
        v_sub (center, a, oa);
        memcpy (rd, delta, sizeof rd);
        baba = v_dot (ba, ba);
        bard = v_dot (ba, rd);
        baoa = v_dot (ba, oa);
        rdoa = v_dot (rd, oa);
        oaoa = v_dot (oa, oa);
        combined = radius + sphyl->radius;
        A = baba * v_dot (rd, rd) - bard * bard;
        B = baba * rdoa - baoa * bard;
        C = baba * oaoa - baoa * baoa - combined * combined * baba;
        if (A > 1e-12)
        {
            h = B * B - A * C;
            if (h >= 0.0)
            {
                t = (-B - sqrt (h)) / A;
                y = baoa + t * bard;
                if (y > 0.0 && y < baba && t >= -SOLID_EPS && t <= 1.0 && t < *t_out)
                {
                    gdouble at[3], axis_point[3], rel[3];

                    if (t < 0.0)
                        t = 0.0;
                    v_mad (center, delta, t, at);
                    v_mad (a, ba, y / baba, axis_point);
                    v_sub (at, axis_point, rel);
                    if (v_normalize (rel))
                    {
                        *t_out = t;
                        memcpy (normal, rel, sizeof rel);
                        v_mad (axis_point, rel, sphyl->radius, point);
                    }
                }
            }
        }
    }
    return *t_out <= 1.0;
}

static gboolean
sweep_sphere_convex (const gdouble *center, gdouble radius, const gdouble *delta,
                     const LrgSolidPrim *convex, gdouble *t_out, gdouble *normal, gdouble *point)
{
    gdouble at[3], closest[3], rel[3];
    gboolean inside;
    gdouble dist, speed, t, gap;
    guint iter;

    speed = v_len (delta);
    dist = closest_on_convex (convex, center, closest, &inside);
    if (inside || dist <= radius + SOLID_EPS)
    {
        *t_out = 0.0;
        if (!inside)
            v_sub (center, closest, rel);
        else
        {
            rel[0] = 0.0;
            rel[1] = 1.0;
            rel[2] = 0.0;
            if (convex->n_planes > 0)
                memcpy (rel, convex->planes, sizeof (gdouble) * 3);
        }
        if (!v_normalize (rel))
        {
            rel[0] = 0.0;
            rel[1] = 1.0;
            rel[2] = 0.0;
        }
        memcpy (normal, rel, sizeof rel);
        memcpy (point, closest, sizeof closest);
        return TRUE;
    }
    if (!(speed > 1e-12))
        return FALSE;
    t = 0.0;
    for (iter = 0; iter < 64; iter++)
    {
        v_mad (center, delta, t, at);
        dist = closest_on_convex (convex, at, closest, &inside);
        if (inside || dist <= radius + SOLID_EPS)
        {
            *t_out = t;
            v_sub (at, closest, rel);
            if (!v_normalize (rel))
            {
                rel[0] = 0.0;
                rel[1] = 1.0;
                rel[2] = 0.0;
            }
            memcpy (normal, rel, sizeof rel);
            memcpy (point, closest, sizeof closest);
            return TRUE;
        }
        gap = dist - radius;
        t += gap / speed;
        if (t > 1.0)
            return FALSE;
    }
    /* Still short of the surface inside the segment. Reporting a miss
     * would let the sphere tunnel through the hull. */
    *t_out = t;
    v_mad (center, delta, t, at);
    (void) closest_on_convex (convex, at, closest, &inside);
    v_sub (at, closest, rel);
    if (!v_normalize (rel))
    {
        rel[0] = 0.0;
        rel[1] = 1.0;
        rel[2] = 0.0;
    }
    memcpy (normal, rel, sizeof rel);
    memcpy (point, closest, sizeof closest);
    return TRUE;
}

gboolean
lrg_solid_prims_sweep_sphere (const LrgSolidPrim *static_prims,
                              guint               n_static,
                              const gdouble       center[3],
                              gdouble             radius,
                              const gdouble       delta[3],
                              LrgSolidHit        *hit,
                              gboolean           *did_hit,
                              GError            **error)
{
    gboolean found;
    gdouble best_t, best_n[3], best_p[3];
    guint i, best_i;

    found = FALSE;
    best_t = 2.0;
    best_i = 0;
    memset (best_n, 0, sizeof best_n);
    memset (best_p, 0, sizeof best_p);
    for (i = 0; i < n_static; i++)
    {
        gdouble t, normal[3], point[3];
        gboolean hit_one;

        hit_one = FALSE;
        t = 2.0;
        switch (static_prims[i].kind)
        {
        case LRG_SOLID_PRIM_SPHERE:
            hit_one = sweep_sphere_sphere (center, radius, delta, &static_prims[i], &t, normal, point);
            break;
        case LRG_SOLID_PRIM_BOX:
            hit_one = sweep_sphere_box (center, radius, delta, &static_prims[i], &t, normal, point);
            break;
        case LRG_SOLID_PRIM_SPHYL:
            hit_one = sweep_sphere_sphyl (center, radius, delta, &static_prims[i], &t, normal, point);
            break;
        case LRG_SOLID_PRIM_CONVEX:
            hit_one = sweep_sphere_convex (center, radius, delta, &static_prims[i], &t, normal, point);
            break;
        default:
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 "Sphere sweep against this solid is not supported");
            return FALSE;
        }
        if (!hit_one)
            continue;
        if (!found || t < best_t - 1e-8 || (fabs (t - best_t) <= 1e-8 && i < best_i))
        {
            found = TRUE;
            best_t = t;
            best_i = i;
            memcpy (best_n, normal, sizeof best_n);
            memcpy (best_p, point, sizeof best_p);
        }
    }
    *did_hit = found;
    if (found && hit != NULL)
    {
        float3 (best_p, hit->position);
        float3 (best_n, hit->normal);
        hit->time = (gfloat) best_t;
        hit->distance = (gfloat) (best_t * v_len (delta));
        hit->primitive = best_i;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Constructors                                                             */
/* ------------------------------------------------------------------------ */

static LrgSolidGeometry *
finish_one (LrgSolidPrim *prim, LrgSolidShape shape, const gfloat *transform, GError **error)
{
    LrgSolidGeometry *self;
    gdouble rotation[3][3];
    gdouble translation[3];
    gdouble scale;

    if (!lrg_solid_transform_decompose (transform, rotation, translation, &scale, error))
    {
        lrg_solid_prims_free (prim, 1);
        return NULL;
    }
    prim_transform (prim, rotation, scale, translation);
    self = g_object_new (LRG_TYPE_SOLID_GEOMETRY, NULL);
    self->shape = shape;
    self->depth = 1;
    self->prims = prim;
    self->n_prims = 1;
    return self;
}

LrgSolidGeometry *
lrg_solid_geometry_new_sphere (gfloat radius, const gfloat *transform, GError **error)
{
    LrgSolidPrim *prim;

    if (!isfinite (radius) || !(radius > 0.0f))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Sphere radius must be finite and positive");
        return NULL;
    }
    prim = g_new0 (LrgSolidPrim, 1);
    prim->kind = LRG_SOLID_PRIM_SPHERE;
    prim->radius = (gdouble) radius;
    axis_identity (prim->axis);
    return finish_one (prim, LRG_SOLID_SHAPE_SPHERE, transform, error);
}

LrgSolidGeometry *
lrg_solid_geometry_new_box (const gfloat *half_extents, const gfloat *transform, GError **error)
{
    LrgSolidPrim *prim;
    guint c;

    g_return_val_if_fail (half_extents != NULL, NULL);
    for (c = 0; c < 3; c++)
    {
        if (!isfinite (half_extents[c]) || !(half_extents[c] > 0.0f))
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Box half extents must be finite and positive");
            return NULL;
        }
    }
    prim = g_new0 (LrgSolidPrim, 1);
    prim->kind = LRG_SOLID_PRIM_BOX;
    axis_identity (prim->axis);
    for (c = 0; c < 3; c++)
        prim->half[c] = (gdouble) half_extents[c];
    return finish_one (prim, LRG_SOLID_SHAPE_BOX, transform, error);
}

LrgSolidGeometry *
lrg_solid_geometry_new_sphyl (gfloat radius, gfloat half_length, const gfloat *transform, GError **error)
{
    LrgSolidPrim *prim;

    if (!isfinite (radius) || !(radius > 0.0f) || !isfinite (half_length) || half_length < 0.0f)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Sphyl radius must be positive and half-length must be at least zero");
        return NULL;
    }
    prim = g_new0 (LrgSolidPrim, 1);
    prim->kind = LRG_SOLID_PRIM_SPHYL;
    prim->radius = (gdouble) radius;
    prim->half[1] = (gdouble) half_length;
    axis_identity (prim->axis);
    return finish_one (prim, LRG_SOLID_SHAPE_SPHYL, transform, error);
}

LrgSolidGeometry *
lrg_solid_geometry_new_convex (const gfloat *points, guint n_floats, const gfloat *transform, GError **error)
{
    LrgSolidPrim *prim;
    gdouble *xyz;
    guint n_points, i;
    guint *faces = NULL;
    gdouble *planes = NULL;
    guint n_faces = 0, n_planes = 0;

    if (points == NULL || n_floats < 12 || (n_floats % 3u) != 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex points must be finite x,y,z triples, at least four");
        return NULL;
    }
    n_points = n_floats / 3u;
    if (n_points > LRG_SOLID_MAX_CONVEX_POINTS)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Convex point count exceeds LRG_SOLID_MAX_CONVEX_POINTS");
        return NULL;
    }
    xyz = g_new (gdouble, n_points * 3);
    for (i = 0; i < n_floats; i++)
    {
        if (!isfinite (points[i]))
        {
            g_free (xyz);
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Convex points must be finite");
            return NULL;
        }
        xyz[i] = (gdouble) points[i];
    }
    if (!build_hull (xyz, &n_points, &faces, &n_faces, &planes, &n_planes, error))
    {
        g_free (xyz);
        return NULL;
    }
    prim = g_new0 (LrgSolidPrim, 1);
    prim->kind = LRG_SOLID_PRIM_CONVEX;
    axis_identity (prim->axis);
    prim->points = xyz;
    prim->n_points = n_points;
    prim->faces = faces;
    prim->n_faces = n_faces;
    prim->planes = planes;
    prim->n_planes = n_planes;
    return finish_one (prim, LRG_SOLID_SHAPE_CONVEX, transform, error);
}

static gboolean
child_cycle (LrgSolidGeometry *node, GHashTable *stack, GHashTable *done)
{
    guint i;

    if (g_hash_table_contains (done, node))
        return FALSE;
    if (!g_hash_table_add (stack, node))
        return TRUE;
    for (i = 0; i < node->n_children; i++)
    {
        if (child_cycle (node->children[i], stack, done))
            return TRUE;
    }
    g_hash_table_remove (stack, node);
    g_hash_table_add (done, node);
    return FALSE;
}

LrgSolidGeometry *
lrg_solid_geometry_new_aggregate (LrgSolidGeometry *const *children, guint n_children, GError **error)
{
    LrgSolidGeometry *self;
    GHashTable *stack, *done;
    LrgSolidPrim *prims;
    guint i, total, depth, cursor;

    if (children == NULL || n_children == 0 || n_children > LRG_SOLID_MAX_AGGREGATE_CHILDREN)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Aggregate needs between 1 and LRG_SOLID_MAX_AGGREGATE_CHILDREN children");
        return NULL;
    }
    total = 0;
    depth = 1;
    for (i = 0; i < n_children; i++)
    {
        if (!LRG_IS_SOLID_GEOMETRY (children[i]))
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Aggregate child is not a solid geometry");
            return NULL;
        }
        total += children[i]->n_prims;
        depth = MAX (depth, children[i]->depth + 1);
    }
    if (total == 0 || total > LRG_SOLID_MAX_PRIMITIVES)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Aggregate primitive count is outside the supported budget");
        return NULL;
    }
    if (depth > LRG_SOLID_MAX_DEPTH)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Aggregate nesting exceeds LRG_SOLID_MAX_DEPTH");
        return NULL;
    }
    stack = g_hash_table_new (g_direct_hash, g_direct_equal);
    done = g_hash_table_new (g_direct_hash, g_direct_equal);
    for (i = 0; i < n_children; i++)
    {
        if (child_cycle (children[i], stack, done))
        {
            g_hash_table_unref (stack);
            g_hash_table_unref (done);
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Aggregate child graph contains a cycle");
            return NULL;
        }
    }
    g_hash_table_unref (stack);
    g_hash_table_unref (done);
    prims = g_new0 (LrgSolidPrim, total);
    cursor = 0;
    for (i = 0; i < n_children; i++)
    {
        guint p;

        for (p = 0; p < children[i]->n_prims; p++)
            prim_copy (&children[i]->prims[p], &prims[cursor++]);
    }
    self = g_object_new (LRG_TYPE_SOLID_GEOMETRY, NULL);
    self->shape = LRG_SOLID_SHAPE_AGGREGATE;
    self->depth = depth;
    self->prims = prims;
    self->n_prims = total;
    self->children = g_new (LrgSolidGeometry *, n_children);
    self->n_children = n_children;
    for (i = 0; i < n_children; i++)
        self->children[i] = g_object_ref (children[i]);
    return self;
}

LrgSolidShape
lrg_solid_geometry_get_shape (LrgSolidGeometry *self)
{
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), LRG_SOLID_SHAPE_SPHERE);
    return self->shape;
}

guint
lrg_solid_geometry_get_primitive_count (LrgSolidGeometry *self)
{
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), 0);
    return self->n_prims;
}

gboolean
lrg_solid_geometry_classify_point (LrgSolidGeometry *self, const gfloat *point,
                                   LrgSolidClass *result, GError **error)
{
    gdouble xyz[3];

    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), FALSE);
    g_return_val_if_fail (point != NULL, FALSE);
    g_return_val_if_fail (result != NULL, FALSE);
    xyz[0] = point[0];
    xyz[1] = point[1];
    xyz[2] = point[2];
    if (!v_finite3 (xyz))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid query point is not finite");
        return FALSE;
    }
    return lrg_solid_prims_classify (self->prims, self->n_prims, xyz, result);
}

gboolean
lrg_solid_geometry_overlap (LrgSolidGeometry *self, LrgSolidGeometry *other,
                            LrgSolidContact *contact, gboolean *overlapping, GError **error)
{
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (other), FALSE);
    g_return_val_if_fail (overlapping != NULL, FALSE);
    return lrg_solid_prims_overlap (self->prims, self->n_prims, other->prims, other->n_prims,
                                    contact, overlapping, error);
}

gboolean
lrg_solid_geometry_contains (LrgSolidGeometry *self, LrgSolidGeometry *inner,
                             gboolean *contained, GError **error)
{
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (inner), FALSE);
    g_return_val_if_fail (contained != NULL, FALSE);
    return lrg_solid_prims_contains (self->prims, self->n_prims, inner->prims, inner->n_prims,
                                     contained, error);
}

gboolean
lrg_solid_geometry_sweep_sphere (LrgSolidGeometry *self, const gfloat *center, gfloat radius,
                                 const gfloat *delta, LrgSolidHit *hit, gboolean *did_hit,
                                 GError **error)
{
    gdouble c[3], d[3];
    guint i;

    g_return_val_if_fail (LRG_IS_SOLID_GEOMETRY (self), FALSE);
    g_return_val_if_fail (center != NULL, FALSE);
    g_return_val_if_fail (delta != NULL, FALSE);
    g_return_val_if_fail (did_hit != NULL, FALSE);
    if (!isfinite (radius) || !(radius > 0.0f))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Sweep sphere radius must be finite and positive");
        return FALSE;
    }
    for (i = 0; i < 3; i++)
    {
        c[i] = center[i];
        d[i] = delta[i];
    }
    if (!v_finite3 (c) || !v_finite3 (d))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Sweep sphere center and motion must be finite");
        return FALSE;
    }
    return lrg_solid_prims_sweep_sphere (self->prims, self->n_prims, c, (gdouble) radius, d,
                                         hit, did_hit, error);
}
