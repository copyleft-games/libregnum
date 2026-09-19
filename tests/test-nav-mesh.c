/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <gio/gio.h>
#include <math.h>

static const gdouble square[] = { 0, 0, 0, 2, 0, 0, 2, 0, 2, 0, 0, 2 };
static const guint triangles[] = { 0, 1, 2, 0, 2, 3 };

static void
test_projection (void)
{
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    const gdouble p[] = { 1, 1, 0.25 };
    const gdouble outside[] = { -1, 0, 1 };
    gdouble result[3];
    guint polygon;

    g_assert_true (lrg_nav_mesh_bake (mesh, square, 12, triangles, 6, 45, NULL));
    g_assert_cmpuint (lrg_nav_mesh_get_polygon_count (mesh), ==, 2);
    g_assert_false (lrg_nav_mesh_project (mesh, p, 0.5, result, NULL));
    g_assert_true (lrg_nav_mesh_project (mesh, p, 1, result, &polygon));
    g_assert_cmpuint (polygon, ==, 0);
    g_assert_cmpfloat_with_epsilon (result[0], 1, 1e-9);
    g_assert_cmpfloat (result[1], ==, 0);
    g_assert_cmpfloat_with_epsilon (result[2], 0.25, 1e-9);
    g_assert_true (lrg_nav_mesh_project (mesh, outside, 1, result, NULL));
    g_assert_cmpfloat (result[0], ==, 0);
    g_assert_cmpfloat (result[2], ==, 1);
}

static void
test_path_and_block (void)
{
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    g_autofree gdouble *path = NULL;
    g_autoptr (GError) error = NULL;
    const gdouble start[] = { 1.5, 0, 0.25 };
    const gdouble goal[] = { 0.25, 0, 1.5 };
    guint count;

    g_assert_true (lrg_nav_mesh_bake (mesh, square, 12, triangles, 6, 0, NULL));
    path = lrg_nav_mesh_find_path (mesh, start, goal, 0.01, &count, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (count, ==, 9);
    g_assert_cmpfloat (path[3], ==, 1);
    g_assert_cmpfloat (path[5], ==, 1);
    g_assert_cmpfloat_with_epsilon (path[0], start[0], 1e-9);
    g_assert_cmpfloat_with_epsilon (path[count - 1], goal[2], 1e-9);
    g_clear_pointer (&path, g_free);
    g_assert_true (lrg_nav_mesh_set_enabled (mesh, 1, FALSE));
    path = lrg_nav_mesh_find_path (mesh, start, goal, 0.01, &count, &error);
    g_assert_null (path);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (count, ==, 0);
    g_clear_error (&error);
    g_assert_true (lrg_nav_mesh_set_enabled (mesh, 1, TRUE));
    path = lrg_nav_mesh_find_path (mesh, start, start, 0.01, &count, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (count, ==, 6);
    g_assert_false (lrg_nav_mesh_set_enabled (mesh, 2, TRUE));
}

static void
test_bake_failures (void)
{
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    g_autoptr (GError) error = NULL;
    const guint bad[] = { 0, 1, 99 };
    const guint nonmanifold[] = { 0, 1, 2, 0, 2, 3, 0, 2, 1 };
    const gdouble wall[] = { 0, 0, 0, 0, 1, 0, 0, 0, 1 };
    const gdouble invalid[] = { 0, 0, 0, NAN, 1, 0, 0, 0, 1 };
    const guint one[] = { 0, 1, 2 };

    g_assert_true (lrg_nav_mesh_bake (mesh, square, 12, triangles, 6, 45, NULL));
    g_assert_false (lrg_nav_mesh_bake (mesh, square, 12, bad, 3, 45, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_false (lrg_nav_mesh_bake (mesh, square, 12, nonmanifold, 9, 45, &error));
    g_clear_error (&error);
    g_assert_false (lrg_nav_mesh_bake (mesh, wall, 9, one, 3, 80, &error));
    g_clear_error (&error);
    g_assert_false (lrg_nav_mesh_bake (mesh, invalid, 9, one, 3, 45, &error));
    g_clear_error (&error);
    g_assert_false (lrg_nav_mesh_bake (mesh, square, 12, triangles, 6, NAN, &error));
    g_assert_cmpuint (lrg_nav_mesh_get_polygon_count (mesh), ==, 2);
}

static void
test_disconnected (void)
{
    const gdouble vertices[] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, 5, 0, 0, 6, 0, 0, 5, 0, 1 };
    const guint indices[] = { 0, 1, 2, 3, 4, 5 };
    const gdouble a[] = { 0.1, 0, 0.1 }, b[] = { 5.1, 0, 0.1 };
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    g_autoptr (GError) error = NULL;
    g_autofree gdouble *path = NULL;
    guint count;

    g_assert_true (lrg_nav_mesh_bake (mesh, vertices, 18, indices, 6, 45, NULL));
    path = lrg_nav_mesh_find_path (mesh, a, b, 0.01, &count, &error);
    g_assert_null (path);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_slopes_and_welding (void)
{
    const gdouble vertices[] = { 0, 0, 0, 2, 1, 0, 2, 1, 2, -0.0, 0, 0, 2, 1, 2, 0, 0, 2 };
    const guint indices[] = { 0, 1, 2, 3, 4, 5 };
    const gdouble a[] = { 1.5, 0.75, 0.25 }, b[] = { 0.25, 0.125, 1.5 };
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    g_autofree gdouble *path = NULL;
    guint count;

    g_assert_false (lrg_nav_mesh_bake (mesh, vertices, 18, indices, 6, 20, NULL));
    g_assert_true (lrg_nav_mesh_bake (mesh, vertices, 18, indices, 6, 30, NULL));
    path = lrg_nav_mesh_find_path (mesh, a, b, 0.001, &count, NULL);
    g_assert_nonnull (path);
    g_assert_cmpuint (count, ==, 9);
    g_assert_cmpfloat (path[4], ==, 0.5);
}

static void
test_obstacle_corridor (void)
{
    g_autoptr (GArray) verts = g_array_new (FALSE, FALSE, sizeof (gdouble));
    g_autoptr (GArray) indices = g_array_new (FALSE, FALSE, sizeof (guint));
    g_autoptr (LrgNavMesh) mesh = lrg_nav_mesh_new ();
    g_autofree gdouble *path = NULL;
    const gdouble start[] = { 0.1, 0, 1.5 }, goal[] = { 2.9, 0, 1.5 };
    guint x, z, i, k, count;

    for (z = 0; z < 3; z++)
        for (x = 0; x < 3; x++)
        {
            gdouble cell[] = { x, 0, z, x + 1, 0, z, x + 1, 0, z + 1, x, 0, z + 1 };
            guint base = verts->len / 3;
            guint faces[] = { base, base + 1, base + 2, base, base + 2, base + 3 };

            if (x == 1 && z == 1)
                continue; /* A hole must not be crossed. */
            g_array_append_vals (verts, cell, 12);
            g_array_append_vals (indices, faces, 6);
        }
    g_assert_true (lrg_nav_mesh_bake (mesh, (gdouble *)verts->data, verts->len,
                                      (guint *)indices->data, indices->len, 45, NULL));
    path = lrg_nav_mesh_find_path (mesh, start, goal, 0.01, &count, NULL);
    g_assert_nonnull (path);
    g_assert_cmpuint (count, >, 9);
    for (i = 3; i < count; i += 3)
        for (k = 0; k <= 20; k++)
        {
            gdouble p[3], result[3];
            guint c;

            for (c = 0; c < 3; c++)
                p[c] = path[i - 3 + c] + (path[i + c] - path[i - 3 + c]) * k / 20.0;
            g_assert_true (lrg_nav_mesh_project (mesh, p, 1e-7, result, NULL));
            g_assert_false (p[0] > 1 && p[0] < 2 && p[2] > 1 && p[2] < 2);
        }
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/nav-mesh/projection", test_projection);
    g_test_add_func ("/nav-mesh/path-block", test_path_and_block);
    g_test_add_func ("/nav-mesh/bake-failures", test_bake_failures);
    g_test_add_func ("/nav-mesh/disconnected", test_disconnected);
    g_test_add_func ("/nav-mesh/slopes-welding", test_slopes_and_welding);
    g_test_add_func ("/nav-mesh/obstacle-corridor", test_obstacle_corridor);
    return g_test_run ();
}
