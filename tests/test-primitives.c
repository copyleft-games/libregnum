/* test-primitives.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for new 3D primitive shapes: Cylinder, Cone, Plane, Grid,
 * Circle, Torus, and IcoSphere.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Cases - LrgCylinder3D
 * ========================================================================== */

static void
test_cylinder3d_new (void)
{
    g_autoptr(LrgCylinder3D) cylinder = NULL;

    cylinder = lrg_cylinder3d_new ();

    g_assert_nonnull (cylinder);
    g_assert_true (LRG_IS_CYLINDER3D (cylinder));
    g_assert_true (LRG_IS_SHAPE3D (cylinder));
    g_assert_true (LRG_IS_SHAPE (cylinder));
}

static void
test_cylinder3d_new_full (void)
{
    g_autoptr(LrgCylinder3D) cylinder = NULL;
    g_autoptr(GrlColor)      color = NULL;

    color = grl_color_new (255, 0, 0, 255);
    cylinder = lrg_cylinder3d_new_full (1.0f, 2.0f, 3.0f,
                                        0.5f, 2.0f, 16,
                                        color);

    g_assert_nonnull (cylinder);
    g_assert_cmpfloat_with_epsilon (lrg_cylinder3d_get_radius (cylinder), 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cylinder3d_get_height (cylinder), 2.0f, 0.001f);
    g_assert_cmpint (lrg_cylinder3d_get_slices (cylinder), ==, 16);
}

static void
test_cylinder3d_properties (void)
{
    g_autoptr(LrgCylinder3D) cylinder = NULL;

    cylinder = lrg_cylinder3d_new ();

    lrg_cylinder3d_set_radius (cylinder, 3.0f);
    lrg_cylinder3d_set_height (cylinder, 5.0f);
    lrg_cylinder3d_set_slices (cylinder, 32);
    lrg_cylinder3d_set_cap_ends (cylinder, FALSE);

    g_assert_cmpfloat_with_epsilon (lrg_cylinder3d_get_radius (cylinder), 3.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cylinder3d_get_height (cylinder), 5.0f, 0.001f);
    g_assert_cmpint (lrg_cylinder3d_get_slices (cylinder), ==, 32);
    g_assert_false (lrg_cylinder3d_get_cap_ends (cylinder));
}

/* ==========================================================================
 * Test Cases - LrgCone3D
 * ========================================================================== */

static void
test_cone3d_new (void)
{
    g_autoptr(LrgCone3D) cone = NULL;

    cone = lrg_cone3d_new ();

    g_assert_nonnull (cone);
    g_assert_true (LRG_IS_CONE3D (cone));
    g_assert_true (LRG_IS_SHAPE3D (cone));
}

static void
test_cone3d_new_full (void)
{
    g_autoptr(LrgCone3D) cone = NULL;
    g_autoptr(GrlColor)  color = NULL;

    color = grl_color_new (0, 255, 0, 255);
    cone = lrg_cone3d_new_full (0.0f, 0.0f, 0.0f,
                                1.0f, 0.0f, 2.0f, 16,
                                color);

    g_assert_nonnull (cone);
    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_radius_bottom (cone), 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_radius_top (cone), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_height (cone), 2.0f, 0.001f);
}

static void
test_cone3d_properties (void)
{
    g_autoptr(LrgCone3D) cone = NULL;

    cone = lrg_cone3d_new ();

    lrg_cone3d_set_radius_bottom (cone, 2.0f);
    lrg_cone3d_set_radius_top (cone, 0.5f);
    lrg_cone3d_set_height (cone, 4.0f);
    lrg_cone3d_set_slices (cone, 24);

    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_radius_bottom (cone), 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_radius_top (cone), 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_cone3d_get_height (cone), 4.0f, 0.001f);
    g_assert_cmpint (lrg_cone3d_get_slices (cone), ==, 24);
}

/* ==========================================================================
 * Test Cases - LrgPlane3D
 * ========================================================================== */

static void
test_plane3d_new (void)
{
    g_autoptr(LrgPlane3D) plane = NULL;

    plane = lrg_plane3d_new ();

    g_assert_nonnull (plane);
    g_assert_true (LRG_IS_PLANE3D (plane));
    g_assert_true (LRG_IS_SHAPE3D (plane));
}

static void
test_plane3d_new_full (void)
{
    g_autoptr(LrgPlane3D) plane = NULL;
    g_autoptr(GrlColor)   color = NULL;

    color = grl_color_new (0, 0, 255, 255);
    plane = lrg_plane3d_new_full (0.0f, 0.0f, 0.0f, 10.0f, 5.0f, color);

    g_assert_nonnull (plane);
    g_assert_cmpfloat_with_epsilon (lrg_plane3d_get_width (plane), 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_plane3d_get_length (plane), 5.0f, 0.001f);
}

static void
test_plane3d_properties (void)
{
    g_autoptr(LrgPlane3D) plane = NULL;

    plane = lrg_plane3d_new ();

    lrg_plane3d_set_width (plane, 20.0f);
    lrg_plane3d_set_length (plane, 15.0f);

    g_assert_cmpfloat_with_epsilon (lrg_plane3d_get_width (plane), 20.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_plane3d_get_length (plane), 15.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgGrid3D
 * ========================================================================== */

static void
test_grid3d_new (void)
{
    g_autoptr(LrgGrid3D) grid = NULL;

    grid = lrg_grid3d_new ();

    g_assert_nonnull (grid);
    g_assert_true (LRG_IS_GRID3D (grid));
    g_assert_true (LRG_IS_SHAPE3D (grid));
}

static void
test_grid3d_new_sized (void)
{
    g_autoptr(LrgGrid3D) grid = NULL;

    grid = lrg_grid3d_new_sized (20, 1.0f);

    g_assert_nonnull (grid);
    g_assert_cmpint (lrg_grid3d_get_slices (grid), ==, 20);
    g_assert_cmpfloat_with_epsilon (lrg_grid3d_get_spacing (grid), 1.0f, 0.001f);
}

static void
test_grid3d_properties (void)
{
    g_autoptr(LrgGrid3D) grid = NULL;

    grid = lrg_grid3d_new ();

    lrg_grid3d_set_slices (grid, 50);
    lrg_grid3d_set_spacing (grid, 0.5f);

    g_assert_cmpint (lrg_grid3d_get_slices (grid), ==, 50);
    g_assert_cmpfloat_with_epsilon (lrg_grid3d_get_spacing (grid), 0.5f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgCircle3D
 * ========================================================================== */

static void
test_circle3d_new (void)
{
    g_autoptr(LrgCircle3D) circle = NULL;

    circle = lrg_circle3d_new ();

    g_assert_nonnull (circle);
    g_assert_true (LRG_IS_CIRCLE3D (circle));
    g_assert_true (LRG_IS_SHAPE3D (circle));
}

static void
test_circle3d_new_full (void)
{
    g_autoptr(LrgCircle3D) circle = NULL;
    g_autoptr(GrlColor)    color = NULL;

    color = grl_color_new (255, 255, 0, 255);
    circle = lrg_circle3d_new_full (0.0f, 0.0f, 0.0f,
                                    2.5f, 64,
                                    color);

    g_assert_nonnull (circle);
    g_assert_cmpfloat_with_epsilon (lrg_circle3d_get_radius (circle), 2.5f, 0.001f);
    g_assert_cmpint (lrg_circle3d_get_vertices (circle), ==, 64);
}

static void
test_circle3d_properties (void)
{
    g_autoptr(LrgCircle3D) circle = NULL;

    circle = lrg_circle3d_new ();

    lrg_circle3d_set_radius (circle, 5.0f);
    lrg_circle3d_set_vertices (circle, 128);
    lrg_circle3d_set_fill_type (circle, LRG_CIRCLE_FILL_TRIFAN);

    g_assert_cmpfloat_with_epsilon (lrg_circle3d_get_radius (circle), 5.0f, 0.001f);
    g_assert_cmpint (lrg_circle3d_get_vertices (circle), ==, 128);
    g_assert_cmpint (lrg_circle3d_get_fill_type (circle), ==, LRG_CIRCLE_FILL_TRIFAN);
}

/* ==========================================================================
 * Test Cases - LrgTorus3D
 * ========================================================================== */

static void
test_torus3d_new (void)
{
    g_autoptr(LrgTorus3D) torus = NULL;

    torus = lrg_torus3d_new ();

    g_assert_nonnull (torus);
    g_assert_true (LRG_IS_TORUS3D (torus));
    g_assert_true (LRG_IS_SHAPE3D (torus));
}

static void
test_torus3d_new_full (void)
{
    g_autoptr(LrgTorus3D) torus = NULL;
    g_autoptr(GrlColor)   color = NULL;

    color = grl_color_new (255, 0, 255, 255);
    torus = lrg_torus3d_new_full (0.0f, 0.0f, 0.0f,
                                  2.0f, 0.5f, 32, 16,
                                  color);

    g_assert_nonnull (torus);
    g_assert_cmpfloat_with_epsilon (lrg_torus3d_get_major_radius (torus), 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_torus3d_get_minor_radius (torus), 0.5f, 0.001f);
    g_assert_cmpint (lrg_torus3d_get_major_segments (torus), ==, 32);
    g_assert_cmpint (lrg_torus3d_get_minor_segments (torus), ==, 16);
}

static void
test_torus3d_properties (void)
{
    g_autoptr(LrgTorus3D) torus = NULL;

    torus = lrg_torus3d_new ();

    lrg_torus3d_set_major_radius (torus, 3.0f);
    lrg_torus3d_set_minor_radius (torus, 1.0f);
    lrg_torus3d_set_major_segments (torus, 48);
    lrg_torus3d_set_minor_segments (torus, 24);

    g_assert_cmpfloat_with_epsilon (lrg_torus3d_get_major_radius (torus), 3.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_torus3d_get_minor_radius (torus), 1.0f, 0.001f);
    g_assert_cmpint (lrg_torus3d_get_major_segments (torus), ==, 48);
    g_assert_cmpint (lrg_torus3d_get_minor_segments (torus), ==, 24);
}

/* ==========================================================================
 * Test Cases - LrgIcoSphere3D
 * ========================================================================== */

static void
test_icosphere3d_new (void)
{
    g_autoptr(LrgIcoSphere3D) icosphere = NULL;

    icosphere = lrg_icosphere3d_new ();

    g_assert_nonnull (icosphere);
    g_assert_true (LRG_IS_ICOSPHERE3D (icosphere));
    g_assert_true (LRG_IS_SHAPE3D (icosphere));
}

static void
test_icosphere3d_new_full (void)
{
    g_autoptr(LrgIcoSphere3D) icosphere = NULL;
    g_autoptr(GrlColor)       color = NULL;

    color = grl_color_new (0, 255, 255, 255);
    icosphere = lrg_icosphere3d_new_full (0.0f, 0.0f, 0.0f,
                                          2.0f, 3,
                                          color);

    g_assert_nonnull (icosphere);
    g_assert_cmpfloat_with_epsilon (lrg_icosphere3d_get_radius (icosphere), 2.0f, 0.001f);
    g_assert_cmpint (lrg_icosphere3d_get_subdivisions (icosphere), ==, 3);
}

static void
test_icosphere3d_properties (void)
{
    g_autoptr(LrgIcoSphere3D) icosphere = NULL;

    icosphere = lrg_icosphere3d_new ();

    lrg_icosphere3d_set_radius (icosphere, 5.0f);
    lrg_icosphere3d_set_subdivisions (icosphere, 4);

    g_assert_cmpfloat_with_epsilon (lrg_icosphere3d_get_radius (icosphere), 5.0f, 0.001f);
    g_assert_cmpint (lrg_icosphere3d_get_subdivisions (icosphere), ==, 4);
}

/* ==========================================================================
 * Test Cases - Shape3D Base Properties
 * ========================================================================== */

static void
test_shape3d_position (void)
{
    g_autoptr(LrgCylinder3D) shape = NULL;
    GrlVector3              *pos;

    shape = lrg_cylinder3d_new ();

    lrg_shape3d_set_position_xyz (LRG_SHAPE3D (shape), 10.0f, 20.0f, 30.0f);

    pos = lrg_shape3d_get_position (LRG_SHAPE3D (shape));

    g_assert_nonnull (pos);
    g_assert_cmpfloat_with_epsilon (pos->x, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pos->y, 20.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pos->z, 30.0f, 0.001f);
}

static void
test_shape3d_wireframe (void)
{
    g_autoptr(LrgCone3D) shape = NULL;

    shape = lrg_cone3d_new ();

    /* Default should be FALSE */
    g_assert_false (lrg_shape3d_get_wireframe (LRG_SHAPE3D (shape)));

    lrg_shape3d_set_wireframe (LRG_SHAPE3D (shape), TRUE);
    g_assert_true (lrg_shape3d_get_wireframe (LRG_SHAPE3D (shape)));
}

static void
test_shape_color (void)
{
    g_autoptr(LrgPlane3D) shape = NULL;
    g_autoptr(GrlColor)   color = NULL;
    GrlColor             *retrieved;

    shape = lrg_plane3d_new ();
    color = grl_color_new (100, 150, 200, 255);

    lrg_shape_set_color (LRG_SHAPE (shape), color);

    retrieved = lrg_shape_get_color (LRG_SHAPE (shape));
    g_assert_nonnull (retrieved);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgCylinder3D */
    g_test_add_func ("/primitives/cylinder3d/new", test_cylinder3d_new);
    g_test_add_func ("/primitives/cylinder3d/new-full", test_cylinder3d_new_full);
    g_test_add_func ("/primitives/cylinder3d/properties", test_cylinder3d_properties);

    /* LrgCone3D */
    g_test_add_func ("/primitives/cone3d/new", test_cone3d_new);
    g_test_add_func ("/primitives/cone3d/new-full", test_cone3d_new_full);
    g_test_add_func ("/primitives/cone3d/properties", test_cone3d_properties);

    /* LrgPlane3D */
    g_test_add_func ("/primitives/plane3d/new", test_plane3d_new);
    g_test_add_func ("/primitives/plane3d/new-full", test_plane3d_new_full);
    g_test_add_func ("/primitives/plane3d/properties", test_plane3d_properties);

    /* LrgGrid3D */
    g_test_add_func ("/primitives/grid3d/new", test_grid3d_new);
    g_test_add_func ("/primitives/grid3d/new-sized", test_grid3d_new_sized);
    g_test_add_func ("/primitives/grid3d/properties", test_grid3d_properties);

    /* LrgCircle3D */
    g_test_add_func ("/primitives/circle3d/new", test_circle3d_new);
    g_test_add_func ("/primitives/circle3d/new-full", test_circle3d_new_full);
    g_test_add_func ("/primitives/circle3d/properties", test_circle3d_properties);

    /* LrgTorus3D */
    g_test_add_func ("/primitives/torus3d/new", test_torus3d_new);
    g_test_add_func ("/primitives/torus3d/new-full", test_torus3d_new_full);
    g_test_add_func ("/primitives/torus3d/properties", test_torus3d_properties);

    /* LrgIcoSphere3D */
    g_test_add_func ("/primitives/icosphere3d/new", test_icosphere3d_new);
    g_test_add_func ("/primitives/icosphere3d/new-full", test_icosphere3d_new_full);
    g_test_add_func ("/primitives/icosphere3d/properties", test_icosphere3d_properties);

    /* Shape3D base class tests */
    g_test_add_func ("/primitives/shape3d/position", test_shape3d_position);
    g_test_add_func ("/primitives/shape3d/wireframe", test_shape3d_wireframe);
    g_test_add_func ("/primitives/shape/color", test_shape_color);

    return g_test_run ();
}
