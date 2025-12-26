/* lrg-profiler.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Performance profiling implementation.
 */

#include "config.h"
#include "lrg-profiler.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * Profiler Sample
 * ========================================================================== */

struct _LrgProfilerSample
{
    gchar  *name;
    gint64  start_time;
    gint64  duration_us;
};

static LrgProfilerSample *
lrg_profiler_sample_new (const gchar *name,
                         gint64       duration_us)
{
    LrgProfilerSample *sample;

    sample = g_slice_new0 (LrgProfilerSample);
    sample->name = g_strdup (name);
    sample->duration_us = duration_us;

    return sample;
}

LrgProfilerSample *
lrg_profiler_sample_copy (const LrgProfilerSample *self)
{
    if (self == NULL)
        return NULL;

    return lrg_profiler_sample_new (self->name, self->duration_us);
}

void
lrg_profiler_sample_free (LrgProfilerSample *self)
{
    if (self == NULL)
        return;

    g_free (self->name);
    g_slice_free (LrgProfilerSample, self);
}

const gchar *
lrg_profiler_sample_get_name (const LrgProfilerSample *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

gint64
lrg_profiler_sample_get_duration_us (const LrgProfilerSample *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->duration_us;
}

gdouble
lrg_profiler_sample_get_duration_ms (const LrgProfilerSample *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return (gdouble)self->duration_us / 1000.0;
}

G_DEFINE_BOXED_TYPE (LrgProfilerSample, lrg_profiler_sample,
                     lrg_profiler_sample_copy,
                     lrg_profiler_sample_free)

/* ==========================================================================
 * Section Data (internal)
 * ========================================================================== */

typedef struct
{
    gchar  *name;
    gint64  start_time;
    GQueue *samples;       /* GQueue of LrgProfilerSample */
    gint64  total_us;
    gint64  min_us;
    gint64  max_us;
} SectionData;

static SectionData *
section_data_new (const gchar *name)
{
    SectionData *data;

    data = g_slice_new0 (SectionData);
    data->name = g_strdup (name);
    data->samples = g_queue_new ();
    data->min_us = G_MAXINT64;
    data->max_us = 0;

    return data;
}

static void
section_data_free (SectionData *data)
{
    if (data == NULL)
        return;

    g_free (data->name);
    g_queue_free_full (data->samples, (GDestroyNotify)lrg_profiler_sample_free);
    g_slice_free (SectionData, data);
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgProfiler
{
    GObject      parent_instance;

    gboolean     enabled;
    guint        max_samples;

    GHashTable  *sections;      /* name -> SectionData */
    GHashTable  *active;        /* name -> start_time (gint64) */

    gint64       frame_start;
    gint64       last_frame_time_us;
    gdouble      fps;

    guint        frame_count;
    gint64       fps_timer_start;
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgProfiler, lrg_profiler, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgProfiler *default_profiler = NULL;

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_profiler_finalize (GObject *object)
{
    LrgProfiler *self = LRG_PROFILER (object);

    g_hash_table_destroy (self->sections);
    g_hash_table_destroy (self->active);

    if (default_profiler == self)
        default_profiler = NULL;

    G_OBJECT_CLASS (lrg_profiler_parent_class)->finalize (object);
}

static void
lrg_profiler_class_init (LrgProfilerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_profiler_finalize;
}

static void
lrg_profiler_init (LrgProfiler *self)
{
    self->enabled = FALSE;
    self->max_samples = 60;

    self->sections = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            NULL, (GDestroyNotify)section_data_free);
    self->active = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);

    self->frame_start = 0;
    self->last_frame_time_us = 0;
    self->fps = 0.0;
    self->frame_count = 0;
    self->fps_timer_start = g_get_monotonic_time ();

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Created profiler");
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

LrgProfiler *
lrg_profiler_get_default (void)
{
    if (default_profiler == NULL)
        default_profiler = lrg_profiler_new ();

    return default_profiler;
}

LrgProfiler *
lrg_profiler_new (void)
{
    return g_object_new (LRG_TYPE_PROFILER, NULL);
}

/* ==========================================================================
 * Profiling Control
 * ========================================================================== */

gboolean
lrg_profiler_is_enabled (LrgProfiler *self)
{
    g_return_val_if_fail (LRG_IS_PROFILER (self), FALSE);
    return self->enabled;
}

void
lrg_profiler_set_enabled (LrgProfiler *self,
                          gboolean     enabled)
{
    g_return_if_fail (LRG_IS_PROFILER (self));
    self->enabled = enabled;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Profiler %s",
               enabled ? "enabled" : "disabled");
}

guint
lrg_profiler_get_max_samples (LrgProfiler *self)
{
    g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
    return self->max_samples;
}

void
lrg_profiler_set_max_samples (LrgProfiler *self,
                              guint        max_samples)
{
    g_return_if_fail (LRG_IS_PROFILER (self));
    self->max_samples = MAX (1, max_samples);
}

/* ==========================================================================
 * Section Timing
 * ========================================================================== */

void
lrg_profiler_begin_section (LrgProfiler *self,
                            const gchar *name)
{
    gint64 *start_time;

    g_return_if_fail (LRG_IS_PROFILER (self));
    g_return_if_fail (name != NULL);

    if (!self->enabled)
        return;

    start_time = g_new (gint64, 1);
    *start_time = g_get_monotonic_time ();

    g_hash_table_insert (self->active, g_strdup (name), start_time);
}

void
lrg_profiler_end_section (LrgProfiler *self,
                          const gchar *name)
{
    gint64 end_time;
    gint64 *start_time;
    gint64 duration;
    SectionData *section;
    LrgProfilerSample *sample;

    g_return_if_fail (LRG_IS_PROFILER (self));
    g_return_if_fail (name != NULL);

    if (!self->enabled)
        return;

    end_time = g_get_monotonic_time ();

    start_time = g_hash_table_lookup (self->active, name);
    if (start_time == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_DEBUG,
                     "end_section called without matching begin_section: %s", name);
        return;
    }

    duration = end_time - *start_time;
    g_hash_table_remove (self->active, name);

    /* Get or create section data */
    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL)
    {
        section = section_data_new (name);
        g_hash_table_insert (self->sections, section->name, section);
    }

    /* Add sample */
    sample = lrg_profiler_sample_new (name, duration);
    g_queue_push_tail (section->samples, sample);

    /* Trim old samples */
    while (g_queue_get_length (section->samples) > self->max_samples)
    {
        LrgProfilerSample *old = g_queue_pop_head (section->samples);
        section->total_us -= old->duration_us;
        lrg_profiler_sample_free (old);
    }

    /* Update stats */
    section->total_us += duration;
    if (duration < section->min_us)
        section->min_us = duration;
    if (duration > section->max_us)
        section->max_us = duration;
}

void
lrg_profiler_begin_frame (LrgProfiler *self)
{
    g_return_if_fail (LRG_IS_PROFILER (self));

    if (!self->enabled)
        return;

    self->frame_start = g_get_monotonic_time ();
}

void
lrg_profiler_end_frame (LrgProfiler *self)
{
    gint64 now;
    gint64 elapsed;

    g_return_if_fail (LRG_IS_PROFILER (self));

    if (!self->enabled)
        return;

    now = g_get_monotonic_time ();

    if (self->frame_start > 0)
        self->last_frame_time_us = now - self->frame_start;

    /* Update FPS counter */
    self->frame_count++;
    elapsed = now - self->fps_timer_start;

    if (elapsed >= 1000000)  /* 1 second */
    {
        self->fps = (gdouble)self->frame_count / ((gdouble)elapsed / 1000000.0);
        self->frame_count = 0;
        self->fps_timer_start = now;
    }
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

GList *
lrg_profiler_get_section_names (LrgProfiler *self)
{
    g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);
    return g_hash_table_get_keys (self->sections);
}

LrgProfilerSample *
lrg_profiler_get_last_sample (LrgProfiler *self,
                              const gchar *name)
{
    SectionData *section;
    LrgProfilerSample *sample;

    g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL)
        return NULL;

    sample = g_queue_peek_tail (section->samples);
    if (sample == NULL)
        return NULL;

    return lrg_profiler_sample_copy (sample);
}

gdouble
lrg_profiler_get_average_ms (LrgProfiler *self,
                             const gchar *name)
{
    SectionData *section;
    guint count;

    g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
    g_return_val_if_fail (name != NULL, 0.0);

    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL)
        return 0.0;

    count = g_queue_get_length (section->samples);
    if (count == 0)
        return 0.0;

    return ((gdouble)section->total_us / (gdouble)count) / 1000.0;
}

gdouble
lrg_profiler_get_min_ms (LrgProfiler *self,
                         const gchar *name)
{
    SectionData *section;

    g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
    g_return_val_if_fail (name != NULL, 0.0);

    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL || section->min_us == G_MAXINT64)
        return 0.0;

    return (gdouble)section->min_us / 1000.0;
}

gdouble
lrg_profiler_get_max_ms (LrgProfiler *self,
                         const gchar *name)
{
    SectionData *section;

    g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
    g_return_val_if_fail (name != NULL, 0.0);

    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL)
        return 0.0;

    return (gdouble)section->max_us / 1000.0;
}

guint
lrg_profiler_get_sample_count (LrgProfiler *self,
                               const gchar *name)
{
    SectionData *section;

    g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    section = g_hash_table_lookup (self->sections, name);
    if (section == NULL)
        return 0;

    return g_queue_get_length (section->samples);
}

gdouble
lrg_profiler_get_frame_time_ms (LrgProfiler *self)
{
    g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
    return (gdouble)self->last_frame_time_us / 1000.0;
}

gdouble
lrg_profiler_get_fps (LrgProfiler *self)
{
    g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
    return self->fps;
}

void
lrg_profiler_clear (LrgProfiler *self)
{
    g_return_if_fail (LRG_IS_PROFILER (self));

    g_hash_table_remove_all (self->sections);
    g_hash_table_remove_all (self->active);

    self->last_frame_time_us = 0;
    self->fps = 0.0;
    self->frame_count = 0;
    self->fps_timer_start = g_get_monotonic_time ();

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Profiler cleared");
}

void
lrg_profiler_clear_section (LrgProfiler *self,
                            const gchar *name)
{
    g_return_if_fail (LRG_IS_PROFILER (self));
    g_return_if_fail (name != NULL);

    g_hash_table_remove (self->sections, name);
    g_hash_table_remove (self->active, name);
}
