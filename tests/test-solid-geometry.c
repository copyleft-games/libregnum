/* test-solid-geometry.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Closed-solid classification, contacts, sweeps and generation-tagged
 * model slots. A point inside a box must report inside; a triangle shell
 * does not provide that result.
 */

#include <gio/gio.h>
#include <glib.h>
#include <math.h>
#include <string.h>

#include "physics/lrg-solid-geometry.h"
#include "physics/lrg-collision-model.h"

static void
identity (gfloat *m)
{
    memset (m, 0, sizeof (gfloat) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void
test_sphere_classification (void)
{
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(GError) error = NULL;
    LrgSolidClass klass;
    gfloat point[3];

    sphere = lrg_solid_geometry_new_sphere (1.0f, NULL, &error);
    g_assert_no_error (error);
    point[0] = 0.0f;
    point[1] = 0.0f;
    point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (sphere, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);
    point[0] = 1.0f;
    g_assert_true (lrg_solid_geometry_classify_point (sphere, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
    point[0] = 1.2f;
    g_assert_true (lrg_solid_geometry_classify_point (sphere, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_OUTSIDE);
}

static void
test_box_contains_sphere (void)
{
    g_autoptr(LrgSolidGeometry) box = NULL;
    g_autoptr(LrgSolidGeometry) inner = NULL;
    g_autoptr(LrgSolidGeometry) outer = NULL;
    g_autoptr(GError) error = NULL;
    gfloat half[3];
    gboolean contained;
    LrgSolidClass klass;
    gfloat point[3];

    half[0] = half[1] = half[2] = 1.0f;
    box = lrg_solid_geometry_new_box (half, NULL, &error);
    g_assert_no_error (error);
    inner = lrg_solid_geometry_new_sphere (0.2f, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (box, inner, &contained, &error));
    g_assert_true (contained);
    outer = lrg_solid_geometry_new_sphere (1.5f, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (box, outer, &contained, &error));
    g_assert_false (contained);
    point[0] = point[1] = point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (box, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);
}

static void
test_sphere_overlap_depth (void)
{
    g_autoptr(LrgSolidGeometry) a = NULL;
    g_autoptr(LrgSolidGeometry) b = NULL;
    g_autoptr(GError) error = NULL;
    gfloat transform[16];
    LrgSolidContact contact;
    gboolean overlapping;

    identity (transform);
    transform[12] = 1.5f;
    a = lrg_solid_geometry_new_sphere (1.0f, NULL, &error);
    b = lrg_solid_geometry_new_sphere (1.0f, transform, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_solid_geometry_overlap (a, b, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.5f, 1e-4f);
    g_assert_cmpfloat_with_epsilon (contact.normal[0], 1.0f, 1e-4f);
    g_assert_cmpfloat_with_epsilon (contact.normal[1], 0.0f, 1e-4f);
    g_assert_cmpfloat_with_epsilon (contact.point[0], 1.0f, 1e-3f);
}

static void
test_sphere_box_witness_on_first (void)
{
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(LrgSolidGeometry) box = NULL;
    g_autoptr(GError) error = NULL;
    gfloat half[3], transform[16];
    LrgSolidContact contact;
    gboolean overlapping;

    half[0] = half[1] = half[2] = 1.0f;
    identity (transform);
    transform[12] = 1.5f;
    sphere = lrg_solid_geometry_new_sphere (1.0f, NULL, &error);
    box = lrg_solid_geometry_new_box (half, transform, &error);
    g_assert_true (lrg_solid_geometry_overlap (sphere, box, &contact, &overlapping, &error));
    g_assert_no_error (error);
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.5f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.normal[0], 1.0f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.point[0], 1.0f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.point[1], 0.0f, 1e-3f);
}

static void
test_box_overlap (void)
{
    g_autoptr(LrgSolidGeometry) a = NULL;
    g_autoptr(LrgSolidGeometry) b = NULL;
    g_autoptr(GError) error = NULL;
    gfloat half[3], transform[16];
    LrgSolidContact contact;
    gboolean overlapping;

    half[0] = half[1] = half[2] = 1.0f;
    identity (transform);
    transform[12] = 1.5f;
    a = lrg_solid_geometry_new_box (half, NULL, &error);
    b = lrg_solid_geometry_new_box (half, transform, &error);
    g_assert_true (lrg_solid_geometry_overlap (a, b, &contact, &overlapping, &error));
    g_assert_no_error (error);
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.5f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.normal[0], 1.0f, 1e-3f);
}

static void
test_nonuniform_scale_rejected (void)
{
    g_autoptr(GError) error = NULL;
    gfloat transform[16];
    LrgSolidGeometry *sphere;

    identity (transform);
    transform[0] = 2.0f;
    sphere = lrg_solid_geometry_new_sphere (1.0f, transform, &error);
    g_assert_null (sphere);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_sweep_sphere_and_box (void)
{
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(LrgSolidGeometry) box = NULL;
    g_autoptr(GError) error = NULL;
    gfloat half[3], center[3], delta[3];
    LrgSolidHit hit;
    gboolean did_hit;

    sphere = lrg_solid_geometry_new_sphere (0.5f, NULL, &error);
    center[0] = -2.0f;
    center[1] = 0.0f;
    center[2] = 0.0f;
    delta[0] = 4.0f;
    delta[1] = 0.0f;
    delta[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_sweep_sphere (sphere, center, 0.5f, delta, &hit, &did_hit, &error));
    g_assert_true (did_hit);
    g_assert_cmpfloat_with_epsilon (hit.time, 0.25f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (hit.normal[0], -1.0f, 1e-3f);

    half[0] = half[1] = half[2] = 1.0f;
    box = lrg_solid_geometry_new_box (half, NULL, &error);
    center[0] = -3.0f;
    g_assert_true (lrg_solid_geometry_sweep_sphere (box, center, 0.5f, delta, &hit, &did_hit, &error));
    g_assert_true (did_hit);
    g_assert_cmpfloat_with_epsilon (hit.time, 0.375f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (hit.normal[0], -1.0f, 1e-3f);
}

static void
test_convex_tetrahedron (void)
{
    g_autoptr(LrgSolidGeometry) hull = NULL;
    g_autoptr(GError) error = NULL;
    gfloat points[12];
    gfloat query[3];
    LrgSolidClass klass;

    points[0] = 1.0f;
    points[1] = 1.0f;
    points[2] = 1.0f;
    points[3] = 1.0f;
    points[4] = -1.0f;
    points[5] = -1.0f;
    points[6] = -1.0f;
    points[7] = 1.0f;
    points[8] = -1.0f;
    points[9] = -1.0f;
    points[10] = -1.0f;
    points[11] = 1.0f;
    hull = lrg_solid_geometry_new_convex (points, 12, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (hull);
    query[0] = query[1] = query[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (hull, query, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);
    query[0] = query[1] = query[2] = 3.0f;
    g_assert_true (lrg_solid_geometry_classify_point (hull, query, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_OUTSIDE);
}

static void
test_aggregate_and_unsupported_pair (void)
{
    g_autoptr(LrgSolidGeometry) a = NULL;
    g_autoptr(LrgSolidGeometry) b = NULL;
    g_autoptr(LrgSolidGeometry) both = NULL;
    g_autoptr(LrgSolidGeometry) box = NULL;
    g_autoptr(LrgSolidGeometry) sphyl = NULL;
    g_autoptr(GError) error = NULL;
    LrgSolidGeometry *children[2];
    gfloat half[3];
    gfloat transform[16];
    gboolean sentinel;

    identity (transform);
    transform[12] = 3.0f;
    a = lrg_solid_geometry_new_sphere (1.0f, NULL, &error);
    b = lrg_solid_geometry_new_sphere (1.0f, transform, &error);
    children[0] = a;
    children[1] = b;
    both = lrg_solid_geometry_new_aggregate (children, 2, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_solid_geometry_get_primitive_count (both), ==, 2);
    g_assert_cmpint (lrg_solid_geometry_get_shape (both), ==, LRG_SOLID_SHAPE_AGGREGATE);

    half[0] = half[1] = half[2] = 1.0f;
    box = lrg_solid_geometry_new_box (half, NULL, &error);
    sphyl = lrg_solid_geometry_new_sphyl (0.5f, 1.0f, NULL, &error);
    sentinel = TRUE;
    g_assert_false (lrg_solid_geometry_overlap (box, sphyl, NULL, &sentinel, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_assert_true (sentinel);
}

static void
test_world_rollback_and_stale_handle (void)
{
    g_autoptr(LrgSolidWorld) world = NULL;
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(LrgCollisionModel) first = NULL;
    g_autoptr(LrgCollisionModel) second = NULL;
    g_autoptr(GError) error = NULL;
    gfloat transform[16], bad[16], point[3];
    LrgSolidHandle left, right, stale;
    LrgSolidClass klass;
    LrgSolidContact contact;
    gboolean overlapping;
    guint32 group, mask;

    sphere = lrg_solid_geometry_new_sphere (1.0f, NULL, &error);
    identity (transform);
    first = lrg_collision_model_new (sphere, NULL, 1, 0xffffffffu, 7, &error);
    identity (transform);
    transform[12] = 1.5f;
    second = lrg_collision_model_new (sphere, transform, 1, 0xffffffffu, 8, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_collision_model_get_caller_id (first), ==, 7);
    lrg_collision_model_get_filter (first, &group, &mask);
    g_assert_cmpuint (group, ==, 1);

    world = lrg_solid_world_new ();
    g_assert_true (lrg_solid_world_add (world, first, &left, &error));
    g_assert_true (lrg_solid_world_add (world, second, &right, &error));
    g_assert_true (lrg_solid_world_overlap (world, left, right, &contact, &overlapping, &error));
    g_assert_true (overlapping);

    memcpy (bad, transform, sizeof bad);
    bad[0] = 2.0f;
    g_assert_false (lrg_collision_model_set_transform (second, bad, &error));
    g_clear_error (&error);
    g_assert_true (lrg_solid_world_overlap (world, left, right, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.5f, 1e-3f);

    identity (transform);
    transform[12] = 5.0f;
    g_assert_true (lrg_collision_model_set_transform (second, transform, &error));
    g_assert_true (lrg_solid_world_overlap (world, left, right, &contact, &overlapping, &error));
    g_assert_false (overlapping);

    point[0] = point[1] = point[2] = 0.0f;
    g_assert_true (lrg_solid_world_classify_point (world, left, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);

    stale = right;
    g_assert_true (lrg_solid_world_remove (world, right, &error));
    g_assert_true (lrg_solid_world_remove (world, right, &error));
    g_assert_false (lrg_solid_world_overlap (world, left, stale, &contact, &overlapping, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_true (lrg_solid_world_add (world, second, &right, &error));
    g_assert_null (lrg_solid_world_get_model (world, stale, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_true (LRG_IS_COLLISION_MODEL (lrg_solid_world_get_model (world, right, &error)));

    g_object_unref (g_steal_pointer (&world));
    g_assert_true (LRG_IS_SOLID_GEOMETRY (sphere));
}

static void
test_sphyl_classification (void)
{
    g_autoptr(LrgSolidGeometry) sphyl = NULL;
    g_autoptr(GError) error = NULL;
    LrgSolidClass klass;
    gfloat point[3];

    /* Cylinder from y=-1 to y=1, radius 0.5, so the cap surface is y=1.5. */
    sphyl = lrg_solid_geometry_new_sphyl (0.5f, 1.0f, NULL, &error);
    g_assert_no_error (error);
    point[0] = point[1] = point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (sphyl, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);
    point[1] = 1.5f;
    g_assert_true (lrg_solid_geometry_classify_point (sphyl, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
    point[1] = 1.7f;
    g_assert_true (lrg_solid_geometry_classify_point (sphyl, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_OUTSIDE);
    point[0] = 0.5f;
    point[1] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (sphyl, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
}

static void
test_aggregate_budget (void)
{
    g_autoptr(LrgSolidGeometry) leaf = NULL;
    g_autoptr(LrgSolidGeometry) rejected = NULL;
    g_autoptr(GError) error = NULL;
    LrgSolidGeometry *level;
    LrgSolidGeometry *many[64];
    LrgSolidGeometry *groups[5];
    guint i, depth;

    leaf = lrg_solid_geometry_new_sphere (0.1f, NULL, &error);
    g_assert_no_error (error);
    level = g_object_ref (leaf);
    for (depth = 1; depth < LRG_SOLID_MAX_DEPTH; depth++)
    {
        LrgSolidGeometry *next;
        LrgSolidGeometry *one;

        one = level;
        next = lrg_solid_geometry_new_aggregate (&one, 1, &error);
        g_assert_no_error (error);
        g_object_unref (level);
        level = next;
    }
    {
        LrgSolidGeometry *one;

        one = level;
        rejected = lrg_solid_geometry_new_aggregate (&one, 1, &error);
    }
    g_assert_null (rejected);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_object_unref (level);

    for (i = 0; i < 64; i++)
        many[i] = leaf;
    for (i = 0; i < 4; i++)
    {
        groups[i] = lrg_solid_geometry_new_aggregate (many, 64, &error);
        g_assert_no_error (error);
    }
    groups[4] = leaf;
    rejected = lrg_solid_geometry_new_aggregate (groups, 5, &error);
    g_assert_null (rejected);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    for (i = 0; i < 4; i++)
        g_object_unref (groups[i]);
}

static void
test_filter_miss_and_sweep (void)
{
    g_autoptr(LrgSolidWorld) world = NULL;
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(LrgSolidGeometry) box_geom = NULL;
    g_autoptr(LrgCollisionModel) mover = NULL;
    g_autoptr(LrgCollisionModel) wall = NULL;
    g_autoptr(LrgCollisionModel) hidden = NULL;
    g_autoptr(GError) error = NULL;
    gfloat half[3], at[16], delta[3];
    LrgSolidHandle moving, against, masked;
    LrgSolidHit hit;
    gboolean did_hit, overlapping;

    sphere = lrg_solid_geometry_new_sphere (0.5f, NULL, &error);
    half[0] = half[1] = half[2] = 1.0f;
    box_geom = lrg_solid_geometry_new_box (half, NULL, &error);
    identity (at);
    at[12] = -3.0f;
    mover = lrg_collision_model_new (sphere, at, 1, 0xffffffffu, 1, &error);
    wall = lrg_collision_model_new (box_geom, NULL, 1, 0xffffffffu, 2, &error);
    hidden = lrg_collision_model_new (box_geom, NULL, 2, 2, 3, &error);
    world = lrg_solid_world_new ();
    g_assert_true (lrg_solid_world_add (world, mover, &moving, &error));
    g_assert_true (lrg_solid_world_add (world, wall, &against, &error));
    g_assert_true (lrg_solid_world_add (world, hidden, &masked, &error));
    delta[0] = 4.0f;
    delta[1] = 0.0f;
    delta[2] = 0.0f;
    g_assert_true (lrg_solid_world_sweep_sphere (world, moving, delta, against, &hit, &did_hit, &error));
    g_assert_no_error (error);
    g_assert_true (did_hit);
    g_assert_cmpfloat_with_epsilon (hit.time, 0.375f, 1e-3f);
    g_assert_true (lrg_solid_world_overlap (world, moving, masked, NULL, &overlapping, &error));
    g_assert_false (overlapping);
}

static void
test_sphyl_skew_distance (void)
{
    g_autoptr(LrgSolidGeometry) a = NULL;
    g_autoptr(LrgSolidGeometry) apart = NULL;
    g_autoptr(LrgSolidGeometry) close = NULL;
    g_autoptr(GError) error = NULL;
    gfloat apart_m[16], close_m[16];
    LrgSolidContact contact;
    gboolean overlapping;

    /* Local +Y becomes world +X. The other segment stays on Y.
     * Axis distance is the Z translation. Radii sum to 0.5. */
    identity (apart_m);
    apart_m[0] = 0.0f;
    apart_m[1] = -1.0f;
    apart_m[4] = 1.0f;
    apart_m[5] = 0.0f;
    apart_m[14] = 2.0f;
    memcpy (close_m, apart_m, sizeof close_m);
    close_m[14] = 0.4f;
    a = lrg_solid_geometry_new_sphyl (0.25f, 1.0f, NULL, &error);
    apart = lrg_solid_geometry_new_sphyl (0.25f, 1.0f, apart_m, &error);
    close = lrg_solid_geometry_new_sphyl (0.25f, 1.0f, close_m, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_solid_geometry_overlap (a, apart, &contact, &overlapping, &error));
    g_assert_false (overlapping);
    g_assert_true (lrg_solid_geometry_overlap (a, close, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.1f, 1e-3f);
}

static void
test_convex_sat_budget (void)
{
    g_autoptr(LrgSolidGeometry) left = NULL;
    g_autoptr(LrgSolidGeometry) right = NULL;
    g_autoptr(LrgSolidGeometry) near = NULL;
    g_autoptr(GError) error = NULL;
    gfloat points[96];
    gfloat shifted[16];
    LrgSolidContact contact;
    gboolean overlapping;
    guint i;

    /* 32-point puck. Every face and every unique edge is a SAT axis. */
    for (i = 0; i < 16; i++)
    {
        gfloat angle;

        angle = (gfloat) i * (gfloat) (G_PI / 8.0);
        points[i * 6 + 0] = cosf (angle);
        points[i * 6 + 1] = sinf (angle);
        points[i * 6 + 2] = 0.25f;
        points[i * 6 + 3] = points[i * 6 + 0];
        points[i * 6 + 4] = points[i * 6 + 1];
        points[i * 6 + 5] = -0.25f;
    }
    left = lrg_solid_geometry_new_convex (points, 96, NULL, &error);
    identity (shifted);
    shifted[12] = 3.0f;
    right = lrg_solid_geometry_new_convex (points, 96, shifted, &error);
    shifted[12] = 0.5f;
    near = lrg_solid_geometry_new_convex (points, 96, shifted, &error);
    g_assert_no_error (error);
    g_assert_nonnull (left);
    g_assert_true (lrg_solid_geometry_overlap (left, right, &contact, &overlapping, &error));
    g_assert_no_error (error);
    g_assert_false (overlapping);
    g_assert_true (lrg_solid_geometry_overlap (left, near, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat (contact.depth, >, 0.0f);
}

static void
test_flat_cylinder (void)
{
    g_autoptr(LrgSolidGeometry) cylinder = NULL;
    g_autoptr(LrgSolidGeometry) turned = NULL;
    g_autoptr(LrgSolidGeometry) sphere = NULL;
    g_autoptr(LrgSolidGeometry) inside = NULL;
    g_autoptr(LrgSolidGeometry) outside = NULL;
    g_autoptr(LrgSolidGeometry) box = NULL;
    g_autoptr(LrgSolidGeometry) tall = NULL;
    g_autoptr(LrgSolidGeometry) other = NULL;
    g_autoptr(LrgSolidGeometry) hull = NULL;
    g_autoptr(GError) error = NULL;
    LrgSolidClass klass;
    LrgSolidContact contact;
    LrgSolidHit hit;
    gboolean overlapping, contained, did_hit, sentinel;
    gfloat point[3], placed[16], turned_m[16], delta[3], half[3], tetra[12];

    g_assert_null (lrg_solid_geometry_new_cylinder (1.0f, 0.0f, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    cylinder = lrg_solid_geometry_new_cylinder (1.0f, 1.0f, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpint (lrg_solid_geometry_get_shape (cylinder), ==, LRG_SOLID_SHAPE_CYLINDER);
    point[0] = point[1] = point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_INSIDE);
    point[0] = 1.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
    point[0] = 0.0f;
    point[2] = 1.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
    point[0] = 1.2f;
    point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_OUTSIDE);
    /* (1, 0, 1) is the circular rim, so it touches. (1, 1, 1) is the
     * bounding-box corner and must stay outside a round cylinder. */
    point[0] = 1.0f;
    point[1] = 0.0f;
    point[2] = 1.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);
    point[1] = 1.0f;
    g_assert_true (lrg_solid_geometry_classify_point (cylinder, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_OUTSIDE);

    identity (turned_m);
    turned_m[0] = 0.0f;
    turned_m[2] = -1.0f;
    turned_m[5] = 1.0f;
    turned_m[8] = 1.0f;
    turned_m[10] = 0.0f;
    turned = lrg_solid_geometry_new_cylinder (1.0f, 1.0f, turned_m, &error);
    point[0] = 1.0f;
    point[1] = 0.0f;
    point[2] = 0.0f;
    g_assert_true (lrg_solid_geometry_classify_point (turned, point, &klass, &error));
    g_assert_cmpint (klass, ==, LRG_SOLID_CLASS_TOUCHING);

    identity (placed);
    placed[12] = 1.2f;
    sphere = lrg_solid_geometry_new_sphere (0.5f, placed, &error);
    g_assert_true (lrg_solid_geometry_overlap (cylinder, sphere, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.3f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.normal[0], 1.0f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.point[0], 1.0f, 1e-3f);
    g_assert_true (lrg_solid_geometry_overlap (sphere, cylinder, &contact, &overlapping, &error));
    g_assert_true (overlapping);
    g_assert_cmpfloat_with_epsilon (contact.depth, 0.3f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (contact.normal[0], -1.0f, 1e-3f);

    identity (placed);
    placed[12] = 3.0f;
    outside = lrg_solid_geometry_new_sphere (0.5f, placed, &error);
    g_assert_true (lrg_solid_geometry_overlap (cylinder, outside, &contact, &overlapping, &error));
    g_assert_false (overlapping);

    inside = lrg_solid_geometry_new_sphere (0.2f, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (cylinder, inside, &contained, &error));
    g_assert_true (contained);
    g_assert_true (lrg_solid_geometry_contains (outside, cylinder, &contained, &error));
    g_assert_false (contained);
    g_assert_no_error (error);

    half[0] = half[1] = half[2] = 1.0f;
    box = lrg_solid_geometry_new_box (half, NULL, &error);
    tall = lrg_solid_geometry_new_cylinder (0.2f, 2.0f, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (box, tall, &contained, &error));
    g_assert_false (contained);
    g_clear_object (&cylinder);
    cylinder = lrg_solid_geometry_new_cylinder (0.2f, 0.2f, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (box, cylinder, &contained, &error));
    g_assert_true (contained);
    tetra[0] = 1.0f;
    tetra[1] = 1.0f;
    tetra[2] = 1.0f;
    tetra[3] = 1.0f;
    tetra[4] = -1.0f;
    tetra[5] = -1.0f;
    tetra[6] = -1.0f;
    tetra[7] = 1.0f;
    tetra[8] = -1.0f;
    tetra[9] = -1.0f;
    tetra[10] = -1.0f;
    tetra[11] = 1.0f;
    hull = lrg_solid_geometry_new_convex (tetra, 12, NULL, &error);
    g_assert_true (lrg_solid_geometry_contains (hull, cylinder, &contained, &error));
    g_assert_true (contained);

    other = lrg_solid_geometry_new_cylinder (0.4f, 0.4f, NULL, &error);
    sentinel = TRUE;
    g_assert_false (lrg_solid_geometry_overlap (cylinder, other, NULL, &sentinel, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_assert_true (sentinel);
    g_clear_error (&error);
    point[0] = -3.0f;
    point[1] = 0.0f;
    point[2] = 0.0f;
    delta[0] = 4.0f;
    delta[1] = 0.0f;
    delta[2] = 0.0f;
    did_hit = TRUE;
    g_assert_false (lrg_solid_geometry_sweep_sphere (cylinder, point, 0.5f, delta, &hit, &did_hit, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_assert_true (did_hit);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/solid/sphere-classification", test_sphere_classification);
    g_test_add_func ("/solid/box-contains-sphere", test_box_contains_sphere);
    g_test_add_func ("/solid/sphere-overlap", test_sphere_overlap_depth);
    g_test_add_func ("/solid/sphere-box-witness", test_sphere_box_witness_on_first);
    g_test_add_func ("/solid/box-overlap", test_box_overlap);
    g_test_add_func ("/solid/reject-nonuniform-scale", test_nonuniform_scale_rejected);
    g_test_add_func ("/solid/sweep-sphere-box", test_sweep_sphere_and_box);
    g_test_add_func ("/solid/convex-tetrahedron", test_convex_tetrahedron);
    g_test_add_func ("/solid/aggregate-unsupported", test_aggregate_and_unsupported_pair);
    g_test_add_func ("/solid/sphyl-classification", test_sphyl_classification);
    g_test_add_func ("/solid/aggregate-budget", test_aggregate_budget);
    g_test_add_func ("/solid/world-rollback-stale", test_world_rollback_and_stale_handle);
    g_test_add_func ("/solid/filter-and-sweep", test_filter_miss_and_sweep);
    g_test_add_func ("/solid/sphyl-skew", test_sphyl_skew_distance);
    g_test_add_func ("/solid/convex-sat-budget", test_convex_sat_budget);
    g_test_add_func ("/solid/flat-cylinder", test_flat_cylinder);
    return g_test_run ();
}
