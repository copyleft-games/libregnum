/* test-cpu-affinity.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Performance-core detection against fake sysfs trees (x86 hybrid, ARM
 * big.LITTLE, uniform, incomplete), and pin/restore on the live system.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* Writes <root>/devices/system/cpu/cpu<N>/{cpu_capacity,cpufreq/cpuinfo_max_freq};
 * a 0 value leaves that file out. */
static void
write_cpu (const gchar *root,
           guint        cpu,
           guint        capacity,
           guint        max_freq)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *freq_dir = NULL;

    dir = g_strdup_printf ("%s/devices/system/cpu/cpu%u", root, cpu);
    freq_dir = g_build_filename (dir, "cpufreq", NULL);
    g_assert_cmpint (g_mkdir_with_parents (freq_dir, 0700), ==, 0);

    if (capacity > 0)
    {
        g_autofree gchar *path = g_build_filename (dir, "cpu_capacity", NULL);
        g_autofree gchar *text = g_strdup_printf ("%u\n", capacity);

        g_assert_true (g_file_set_contents (path, text, -1, NULL));
    }
    if (max_freq > 0)
    {
        g_autofree gchar *path = g_build_filename (freq_dir, "cpuinfo_max_freq", NULL);
        g_autofree gchar *text = g_strdup_printf ("%u\n", max_freq);

        g_assert_true (g_file_set_contents (path, text, -1, NULL));
    }
}

static gchar *
make_root (void)
{
    g_autoptr(GError) error = NULL;
    gchar            *root;

    root = g_dir_make_tmp ("lrg-cpu-affinity-XXXXXX", &error);
    g_assert_no_error (error);
    return root;
}

/* Recursively removes the fake tree */
static void
remove_tree (const gchar *path)
{
    g_autoptr(GDir) dir = g_dir_open (path, 0, NULL);
    const gchar    *name;

    if (dir != NULL)
    {
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            g_autofree gchar *child = g_build_filename (path, name, NULL);

            if (g_file_test (child, G_FILE_TEST_IS_DIR))
                remove_tree (child);
            else
                g_unlink (child);
        }
    }
    g_rmdir (path);
}

static void
assert_cpus (GArray      *cpus,
             const guint *expect,
             guint        n)
{
    guint i;

    g_assert_nonnull (cpus);
    g_assert_cmpuint (cpus->len, ==, n);
    for (i = 0; i < n; i++)
        g_assert_cmpuint (g_array_index (cpus, guint, i), ==, expect[i]);
}

/* Intel hybrid: equal capacities, P cores at 4.7/4.8 GHz, E cores 3.6 */
static void
test_x86_hybrid (void)
{
    g_autofree gchar *root = make_root ();
    g_autoptr(GArray) fast = NULL;
    static const guint expect[] = { 0, 1, 2, 3, 10 };
    guint             i;

    for (i = 0; i < 4; i++)
        write_cpu (root, i, 1024, i < 2 ? 4700000 : 4800000);
    for (i = 4; i < 10; i++)
        write_cpu (root, i, 1024, 3600000);
    write_cpu (root, 10, 1024, 4700000);

    fast = lrg_cpu_affinity_get_performance_cores (root);
    assert_cpus (fast, expect, G_N_ELEMENTS (expect));
    remove_tree (root);
}

/* ARM big.LITTLE: capacity tells the clusters apart */
static void
test_arm_big_little (void)
{
    g_autofree gchar *root = make_root ();
    g_autoptr(GArray) fast = NULL;
    static const guint expect[] = { 4, 5 };
    guint             i;

    for (i = 0; i < 4; i++)
        write_cpu (root, i, 446, 2016000);
    write_cpu (root, 4, 1024, 3228000);
    write_cpu (root, 5, 1024, 3228000);

    fast = lrg_cpu_affinity_get_performance_cores (root);
    assert_cpus (fast, expect, G_N_ELEMENTS (expect));
    remove_tree (root);
}

/* Uniform CPUs (small turbo spread included) and missing data: no change */
static void
test_not_hybrid (void)
{
    g_autofree gchar *root = make_root ();
    g_autofree gchar *root2 = make_root ();
    g_autofree gchar *root3 = make_root ();
    g_autoptr(GArray) fast = NULL;
    guint             i;

    for (i = 0; i < 8; i++)
        write_cpu (root, i, 1024, i == 0 ? 5000000 : 4900000);
    fast = lrg_cpu_affinity_get_performance_cores (root);
    g_assert_null (fast);

    /* one CPU without any data: cannot rank, so leave the system alone */
    for (i = 0; i < 4; i++)
        write_cpu (root2, i, 1024, i < 2 ? 4700000 : 3600000);
    write_cpu (root2, 4, 0, 0);
    fast = lrg_cpu_affinity_get_performance_cores (root2);
    g_assert_null (fast);

    /* a single CPU */
    write_cpu (root3, 0, 1024, 4700000);
    fast = lrg_cpu_affinity_get_performance_cores (root3);
    g_assert_null (fast);

    remove_tree (root);
    remove_tree (root2);
    remove_tree (root3);
}

/* On the live system: pinning either changes nothing or confines the
 * thread to the reported cores, and restore puts the old mask back */
static void
test_live_pin_restore (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GArray) fast = NULL;
    gboolean          pinned;

    fast = lrg_cpu_affinity_get_performance_cores (NULL);
    pinned = lrg_cpu_affinity_pin_to_performance_cores (&error);
    g_assert_no_error (error);
    g_assert_true (pinned == (fast != NULL));

    if (pinned)
    {
        /* now confined: nothing left to narrow */
        g_autoptr(GArray) again = lrg_cpu_affinity_get_performance_cores (NULL);

        g_assert_null (again);
    }

    lrg_cpu_affinity_restore ();
    lrg_cpu_affinity_restore ();   /* second restore is a no-op */
    {
        g_autoptr(GArray) after = lrg_cpu_affinity_get_performance_cores (NULL);

        g_assert_true ((after == NULL) == (fast == NULL));
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/cpu-affinity/x86-hybrid", test_x86_hybrid);
    g_test_add_func ("/cpu-affinity/arm-big-little", test_arm_big_little);
    g_test_add_func ("/cpu-affinity/not-hybrid", test_not_hybrid);
    g_test_add_func ("/cpu-affinity/live-pin-restore", test_live_pin_restore);

    return g_test_run ();
}
