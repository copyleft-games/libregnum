/* lrg-cpu-affinity.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keeping the main thread on a hybrid CPU's performance cores.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include <errno.h>
#include <string.h>
#include <gio/gio.h>

#ifdef __linux__
#include <sched.h>
#endif

#include "lrg-cpu-affinity.h"
#include "../lrg-log.h"

/* Cores within this fraction of the fastest are "performance" cores, and
 * a CPU whose slowest core is within ALIKE_FRACTION of its fastest is not
 * hybrid at all. */
#define PERFORMANCE_FRACTION (0.90)
#define ALIKE_FRACTION       (0.95)

#ifdef __linux__
/* The affinity in force before the last pin, for restore() */
static cpu_set_t saved_mask;
static gboolean  have_saved_mask = FALSE;
#endif

/*
 * read_cpu_value:
 *
 * Reads one unsigned integer from <root>/devices/system/cpu/cpu<N>/<leaf>.
 * Returns 0 when the file is missing or unreadable (0 is never a real
 * capacity or frequency).
 */
static guint64
read_cpu_value (const gchar *root,
                guint        cpu,
                const gchar *leaf)
{
    g_autofree gchar *path = NULL;
    g_autofree gchar *contents = NULL;
    gchar            *end = NULL;
    guint64           value;

    path = g_strdup_printf ("%s/devices/system/cpu/cpu%u/%s", root, cpu, leaf);
    if (!g_file_get_contents (path, &contents, NULL, NULL))
        return 0;

    value = g_ascii_strtoull (g_strstrip (contents), &end, 10);
    if (end == contents)
        return 0;
    return value;
}

/*
 * list_sysfs_cpus:
 *
 * Every cpu<N> directory under <root>/devices/system/cpu, for a sysfs
 * tree other than the live one (tests).
 */
static GArray *
list_sysfs_cpus (const gchar *root)
{
    g_autofree gchar *dir_path = NULL;
    g_autoptr(GDir)   dir = NULL;
    GArray           *cpus;
    const gchar      *name;

    cpus = g_array_new (FALSE, FALSE, sizeof (guint));
    dir_path = g_build_filename (root, "devices", "system", "cpu", NULL);
    dir = g_dir_open (dir_path, 0, NULL);
    if (dir == NULL)
        return cpus;

    while ((name = g_dir_read_name (dir)) != NULL)
    {
        gchar  *end = NULL;
        guint64 n;

        if (!g_str_has_prefix (name, "cpu") || !g_ascii_isdigit (name[3]))
            continue;
        n = g_ascii_strtoull (name + 3, &end, 10);
        if (end == NULL || *end != '\0' || n > G_MAXUINT)
            continue;
        {
            guint cpu = (guint) n;

            g_array_append_val (cpus, cpu);
        }
    }

    return cpus;
}

static gint
compare_guint (gconstpointer a,
               gconstpointer b)
{
    guint ua = *(const guint *) a;
    guint ub = *(const guint *) b;

    return (ua > ub) - (ua < ub);
}

/*
 * candidate_cpus:
 *
 * The CPUs to choose among: every CPU in a test sysfs tree, or the CPUs
 * the calling thread is currently allowed on (so an outer `taskset` or
 * cgroup limit is respected).
 */
static GArray *
candidate_cpus (const gchar *sysfs_root)
{
    GArray *cpus;

    if (sysfs_root != NULL)
    {
        cpus = list_sysfs_cpus (sysfs_root);
    }
    else
    {
        cpus = g_array_new (FALSE, FALSE, sizeof (guint));
#ifdef __linux__
        {
            cpu_set_t mask;
            guint     cpu;

            CPU_ZERO (&mask);
            if (sched_getaffinity (0, sizeof (mask), &mask) == 0)
            {
                for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
                    if (CPU_ISSET (cpu, &mask))
                        g_array_append_val (cpus, cpu);
            }
        }
#endif
    }

    g_array_sort (cpus, compare_guint);
    return cpus;
}

/*
 * score_spread:
 *
 * Highest and lowest non-zero score. Returns FALSE when any score is
 * missing (0), in which case the source cannot be used to rank cores.
 */
static gboolean
score_spread (const guint64 *scores,
              guint          n,
              guint64       *lo,
              guint64       *hi)
{
    guint i;

    *lo = G_MAXUINT64;
    *hi = 0;
    for (i = 0; i < n; i++)
    {
        if (scores[i] == 0)
            return FALSE;
        *lo = MIN (*lo, scores[i]);
        *hi = MAX (*hi, scores[i]);
    }
    return n > 0;
}

GArray *
lrg_cpu_affinity_get_performance_cores (const gchar *sysfs_root)
{
    g_autoptr(GArray)   cpus = NULL;
    g_autofree guint64 *capacity = NULL;
    g_autofree guint64 *max_freq = NULL;
    const guint64      *scores;
    GArray             *fast;
    guint64             lo;
    guint64             hi;
    guint               i;

    cpus = candidate_cpus (sysfs_root);
    if (cpus->len < 2)
        return NULL;

    /* Gather both rankings; prefer capacity when the kernel differentiates
     * cores by it (ARM big.LITTLE / Apple), else the maximum frequency
     * (x86 hybrids report the same capacity for P and E cores). */
    capacity = g_new0 (guint64, cpus->len);
    max_freq = g_new0 (guint64, cpus->len);
    for (i = 0; i < cpus->len; i++)
    {
        guint cpu = g_array_index (cpus, guint, i);
        const gchar *root = sysfs_root != NULL ? sysfs_root : "/sys";

        capacity[i] = read_cpu_value (root, cpu, "cpu_capacity");
        max_freq[i] = read_cpu_value (root, cpu, "cpufreq/cpuinfo_max_freq");
    }

    if (score_spread (capacity, cpus->len, &lo, &hi) &&
        (gdouble) lo < (gdouble) hi * ALIKE_FRACTION)
        scores = capacity;
    else if (score_spread (max_freq, cpus->len, &lo, &hi) &&
             (gdouble) lo < (gdouble) hi * ALIKE_FRACTION)
        scores = max_freq;
    else
        return NULL;   /* all cores alike, or nothing to rank them by */

    fast = g_array_new (FALSE, FALSE, sizeof (guint));
    for (i = 0; i < cpus->len; i++)
        if ((gdouble) scores[i] >= (gdouble) hi * PERFORMANCE_FRACTION)
            g_array_append_val (fast, g_array_index (cpus, guint, i));

    /* Already confined to performance cores (or none qualify): no change */
    if (fast->len == 0 || fast->len == cpus->len)
    {
        g_array_unref (fast);
        return NULL;
    }

    return fast;
}

gboolean
lrg_cpu_affinity_pin_to_performance_cores (GError **error)
{
#ifdef __linux__
    g_autoptr(GArray) fast = NULL;
    cpu_set_t         previous;
    cpu_set_t         mask;
    guint             i;

    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    fast = lrg_cpu_affinity_get_performance_cores (NULL);
    if (fast == NULL)
        return FALSE;

    CPU_ZERO (&previous);
    if (sched_getaffinity (0, sizeof (previous), &previous) != 0)
    {
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                     "Could not read the CPU affinity: %s", g_strerror (errno));
        return FALSE;
    }

    CPU_ZERO (&mask);
    for (i = 0; i < fast->len; i++)
    {
        guint cpu = g_array_index (fast, guint, i);

        if (cpu < CPU_SETSIZE)
            CPU_SET (cpu, &mask);
    }

    if (sched_setaffinity (0, sizeof (mask), &mask) != 0)
    {
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                     "Could not set the CPU affinity: %s", g_strerror (errno));
        return FALSE;
    }

    /* Keep the original only from the first pin, so pinning twice and
     * restoring once still gets back to where the process started */
    if (!have_saved_mask)
    {
        saved_mask = previous;
        have_saved_mask = TRUE;
    }

    lrg_info (LRG_LOG_DOMAIN_CORE,
              "Main thread kept on %u performance cores", fast->len);
    return TRUE;
#else
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    return FALSE;
#endif
}

void
lrg_cpu_affinity_restore (void)
{
#ifdef __linux__
    if (!have_saved_mask)
        return;

    if (sched_setaffinity (0, sizeof (saved_mask), &saved_mask) != 0)
        lrg_warning (LRG_LOG_DOMAIN_CORE,
                     "Could not restore the CPU affinity: %s", g_strerror (errno));
    have_saved_mask = FALSE;
#endif
}
