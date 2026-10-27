/* lrg-cpu-affinity.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keeping the main thread on a hybrid CPU's performance cores.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-cpu-affinity
 * @title: CPU affinity
 * @short_description: Keep the game loop on a hybrid CPU's fast cores
 *
 * On CPUs that mix fast and slow cores (Intel P/E cores, ARM big.LITTLE,
 * Apple silicon under Asahi) the scheduler moves a game's main thread
 * between core types. Each move lands on a cold cache, and a slow core
 * runs the same frame much longer, which shows up as stutter: low 1% and
 * 5% framerates and frame-time jitter, not as a lower average. Keeping the
 * main thread (and the driver threads it creates afterwards, which inherit
 * its affinity) on the fast cores removes most of that.
 *
 * #LrgEngine does this at startup unless #LrgEngine:pin-performance-cores
 * is %FALSE or the environment has `LRG_CPU_AFFINITY=none`. On CPUs whose
 * cores are all alike, and on platforms other than Linux, nothing changes.
 */

/**
 * lrg_cpu_affinity_get_performance_cores:
 * @sysfs_root: (nullable) (type filename): root of a sysfs tree to read,
 *   or %NULL for the live system (then only CPUs the calling thread may
 *   run on are considered)
 *
 * Finds the fast cores of a hybrid CPU. Cores are ranked by the kernel's
 * `cpu_capacity` when it differs between cores (ARM), else by
 * `cpufreq/cpuinfo_max_freq` (x86 hybrids report equal capacities);
 * cores within 10% of the fastest count as performance cores.
 *
 * Returns: (transfer full) (nullable) (element-type guint): the CPU
 *   numbers of the performance cores, ascending, or %NULL when the CPUs
 *   are all alike (within 5%), the information is unavailable, or every
 *   candidate CPU is already a performance core
 */
LRG_AVAILABLE_IN_ALL
GArray *     lrg_cpu_affinity_get_performance_cores    (const gchar  *sysfs_root);

/**
 * lrg_cpu_affinity_pin_to_performance_cores:
 * @error: (nullable): return location for an error
 *
 * Restricts the calling thread (and threads it creates afterwards) to the
 * performance cores found by lrg_cpu_affinity_get_performance_cores(). The
 * previous affinity is remembered for lrg_cpu_affinity_restore().
 *
 * Returns: %TRUE if the affinity was changed; %FALSE with @error unset
 *   when there is nothing to do (no hybrid CPU, not Linux), %FALSE with
 *   @error set when the system refused
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_cpu_affinity_pin_to_performance_cores (GError      **error);

/**
 * lrg_cpu_affinity_restore:
 *
 * Puts back the calling thread's affinity from before the last successful
 * lrg_cpu_affinity_pin_to_performance_cores(). Does nothing if it never
 * pinned.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_cpu_affinity_restore                  (void);

G_END_DECLS
