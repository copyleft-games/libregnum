/* lrg-profiler.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Performance profiling system.
 *
 * The profiler tracks timing of code sections and provides
 * statistics for performance analysis.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROFILER (lrg_profiler_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProfiler, lrg_profiler, LRG, PROFILER, GObject)

/* ==========================================================================
 * Profiler Sample (Boxed Type)
 * ========================================================================== */

#define LRG_TYPE_PROFILER_SAMPLE (lrg_profiler_sample_get_type ())

/**
 * LrgProfilerSample:
 *
 * A single timing sample from a profiler section.
 */
typedef struct _LrgProfilerSample LrgProfilerSample;

LRG_AVAILABLE_IN_ALL
GType               lrg_profiler_sample_get_type      (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgProfilerSample * lrg_profiler_sample_copy          (const LrgProfilerSample *self);

LRG_AVAILABLE_IN_ALL
void                lrg_profiler_sample_free          (LrgProfilerSample *self);

/**
 * lrg_profiler_sample_get_name:
 * @self: a #LrgProfilerSample
 *
 * Gets the section name.
 *
 * Returns: (transfer none): the section name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_profiler_sample_get_name      (const LrgProfilerSample *self);

/**
 * lrg_profiler_sample_get_duration_us:
 * @self: a #LrgProfilerSample
 *
 * Gets the duration in microseconds.
 *
 * Returns: the duration in microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64              lrg_profiler_sample_get_duration_us (const LrgProfilerSample *self);

/**
 * lrg_profiler_sample_get_duration_ms:
 * @self: a #LrgProfilerSample
 *
 * Gets the duration in milliseconds.
 *
 * Returns: the duration in milliseconds
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_sample_get_duration_ms (const LrgProfilerSample *self);

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_profiler_get_default:
 *
 * Gets the default profiler instance.
 *
 * Returns: (transfer none): the default #LrgProfiler
 */
LRG_AVAILABLE_IN_ALL
LrgProfiler *       lrg_profiler_get_default          (void);

/**
 * lrg_profiler_new:
 *
 * Creates a new profiler.
 *
 * Returns: (transfer full): a new #LrgProfiler
 */
LRG_AVAILABLE_IN_ALL
LrgProfiler *       lrg_profiler_new                  (void);

/* ==========================================================================
 * Profiling Control
 * ========================================================================== */

/**
 * lrg_profiler_is_enabled:
 * @self: a #LrgProfiler
 *
 * Checks if profiling is enabled.
 *
 * Returns: %TRUE if profiling is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_profiler_is_enabled           (LrgProfiler *self);

/**
 * lrg_profiler_set_enabled:
 * @self: a #LrgProfiler
 * @enabled: whether to enable profiling
 *
 * Enables or disables profiling.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_enabled          (LrgProfiler *self,
                                                       gboolean     enabled);

/**
 * lrg_profiler_get_max_samples:
 * @self: a #LrgProfiler
 *
 * Gets the maximum number of samples to keep per section.
 *
 * Returns: the maximum sample count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_profiler_get_max_samples      (LrgProfiler *self);

/**
 * lrg_profiler_set_max_samples:
 * @self: a #LrgProfiler
 * @max_samples: maximum samples per section
 *
 * Sets the maximum number of samples to keep per section.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_max_samples      (LrgProfiler *self,
                                                       guint        max_samples);

/* ==========================================================================
 * Section Timing
 * ========================================================================== */

/**
 * lrg_profiler_begin_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Begins timing a section.
 *
 * Must be paired with lrg_profiler_end_section().
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_begin_section        (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_end_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Ends timing a section.
 *
 * Must be paired with a prior lrg_profiler_begin_section().
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_end_section          (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_begin_frame:
 * @self: a #LrgProfiler
 *
 * Marks the beginning of a frame.
 *
 * This resets per-frame statistics.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_begin_frame          (LrgProfiler *self);

/**
 * lrg_profiler_end_frame:
 * @self: a #LrgProfiler
 *
 * Marks the end of a frame.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_end_frame            (LrgProfiler *self);

/* ==========================================================================
 * Statistics
 * ========================================================================== */

/**
 * lrg_profiler_get_section_names:
 * @self: a #LrgProfiler
 *
 * Gets all section names that have been profiled.
 *
 * Returns: (transfer full) (element-type utf8): list of section names
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_profiler_get_section_names    (LrgProfiler *self);

/**
 * lrg_profiler_get_last_sample:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Gets the most recent sample for a section.
 *
 * Returns: (transfer full) (nullable): the last sample, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerSample * lrg_profiler_get_last_sample      (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_average_ms:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Gets the average duration for a section in milliseconds.
 *
 * Returns: the average duration in milliseconds
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_get_average_ms       (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_min_ms:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Gets the minimum duration for a section in milliseconds.
 *
 * Returns: the minimum duration in milliseconds
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_get_min_ms           (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_max_ms:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Gets the maximum duration for a section in milliseconds.
 *
 * Returns: the maximum duration in milliseconds
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_get_max_ms           (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_sample_count:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Gets the number of samples for a section.
 *
 * Returns: the sample count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_profiler_get_sample_count     (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_frame_time_ms:
 * @self: a #LrgProfiler
 *
 * Gets the last frame time in milliseconds.
 *
 * Returns: the frame time in milliseconds
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_get_frame_time_ms    (LrgProfiler *self);

/**
 * lrg_profiler_get_fps:
 * @self: a #LrgProfiler
 *
 * Gets the current frames per second.
 *
 * Returns: the FPS
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_profiler_get_fps              (LrgProfiler *self);

/**
 * lrg_profiler_clear:
 * @self: a #LrgProfiler
 *
 * Clears all profiling data.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_clear                (LrgProfiler *self);

/**
 * lrg_profiler_clear_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Clears profiling data for a specific section.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_clear_section        (LrgProfiler *self,
                                                       const gchar *name);

G_END_DECLS
