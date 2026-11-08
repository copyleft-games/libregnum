/* lrg-profiler.h
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The CPU (and GPU) profiler.
 *
 * LrgProfiler times named sections.  Sections nest: a section begun
 * while another is open on the same thread becomes its child, so every
 * frame holds a tree of timed events per thread ("lanes").  Frames are
 * delimited by lrg_profiler_begin_frame() / lrg_profiler_end_frame() and
 * kept in a ring of the last N frames, together with per-frame counters
 * and a GPU lane fed by an #LrgGpuTimer.  lrg_profiler_capture() copies
 * frames out of the ring into an #LrgProfilerCapture, which saves to and
 * loads from a `.rprof` file.
 *
 * The 0.8 per-section statistics (last/average/min/max over the last
 * max-samples calls) are kept alongside.  While the profiler is disabled
 * (the default) every call returns after one flag test.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-gpu-timer.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Constants
 * ========================================================================== */

/**
 * LRG_PROFILER_GPU_THREAD:
 *
 * The #LrgProfilerEvent.thread value of GPU events (the GPU lane).
 */
#define LRG_PROFILER_GPU_THREAD (0xFFFFu)

/**
 * LRG_PROFILER_MAX_EVENTS_PER_FRAME:
 *
 * At most this many CPU events are recorded per frame; further sections
 * still update the statistics but are not added to the frame (a runaway
 * loop of sections cannot exhaust memory).
 */
#define LRG_PROFILER_MAX_EVENTS_PER_FRAME (65536)

/**
 * LRG_PROFILER_DEFAULT_FRAME_CAPACITY:
 *
 * The default number of frames the profiler's ring keeps.
 */
#define LRG_PROFILER_DEFAULT_FRAME_CAPACITY (300)

/* ==========================================================================
 * Errors
 * ========================================================================== */

/**
 * LrgProfilerError:
 * @LRG_PROFILER_ERROR_IO: a capture file could not be read or written
 * @LRG_PROFILER_ERROR_PARSE: a capture file is not valid YAML
 * @LRG_PROFILER_ERROR_INVALID: a capture file is YAML but not a valid
 *   `.rprof` document (the message names the key path)
 *
 * Errors of #LrgProfilerCapture loading and saving.
 */
typedef enum
{
	LRG_PROFILER_ERROR_IO,
	LRG_PROFILER_ERROR_PARSE,
	LRG_PROFILER_ERROR_INVALID
} LrgProfilerError;

/**
 * lrg_profiler_error_get_type:
 *
 * Gets the #GType of #LrgProfilerError.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_profiler_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROFILER_ERROR (lrg_profiler_error_get_type ())

/**
 * LRG_PROFILER_ERROR:
 *
 * The error domain of #LrgProfilerError.
 */
#define LRG_PROFILER_ERROR (lrg_profiler_error_quark ())

/**
 * lrg_profiler_error_quark:
 *
 * Gets the error quark of #LrgProfilerError.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_profiler_error_quark (void);

/* ==========================================================================
 * Events and counters
 * ========================================================================== */

/**
 * LrgProfilerEvent:
 * @name: the section name (an interned string, never freed)
 * @thread: the lane: an index into the capture's threads, or
 *   %LRG_PROFILER_GPU_THREAD for GPU events
 * @depth: nesting depth on its lane (0 = top level)
 * @parent: index of the parent event in the same array, or -1
 * @start_us: start in microseconds, relative to the frame's start
 * @duration_us: duration in microseconds (inclusive of children)
 *
 * One timed section in one frame.  Events of a frame are stored in the
 * order they began, so a parent always precedes its children.
 */
typedef struct _LrgProfilerEvent LrgProfilerEvent;

struct _LrgProfilerEvent
{
	const gchar *name;
	guint        thread;
	guint        depth;
	gint         parent;
	gint64       start_us;
	gint64       duration_us;
};

/**
 * lrg_profiler_event_get_type:
 *
 * Gets the boxed #GType of #LrgProfilerEvent.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_profiler_event_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROFILER_EVENT (lrg_profiler_event_get_type ())

/**
 * lrg_profiler_event_copy:
 * @self: an #LrgProfilerEvent
 *
 * Copies an event (the name is interned and shared).
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerEvent * lrg_profiler_event_copy (const LrgProfilerEvent *self);

/**
 * lrg_profiler_event_free:
 * @self: (nullable): an #LrgProfilerEvent from lrg_profiler_event_copy()
 *
 * Frees a copied event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profiler_event_free (LrgProfilerEvent *self);

/**
 * LrgProfilerCounter:
 * @name: the counter name (an interned string, never freed)
 * @value: the value set last in the frame
 *
 * A named per-frame value (draw calls, entities, bytes...), set with
 * lrg_profiler_set_counter().
 */
typedef struct _LrgProfilerCounter LrgProfilerCounter;

struct _LrgProfilerCounter
{
	const gchar *name;
	gdouble      value;
};

/**
 * lrg_profiler_counter_get_type:
 *
 * Gets the boxed #GType of #LrgProfilerCounter.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_profiler_counter_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROFILER_COUNTER (lrg_profiler_counter_get_type ())

/**
 * lrg_profiler_counter_copy:
 * @self: an #LrgProfilerCounter
 *
 * Copies a counter.
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCounter * lrg_profiler_counter_copy (const LrgProfilerCounter *self);

/**
 * lrg_profiler_counter_free:
 * @self: (nullable): an #LrgProfilerCounter from lrg_profiler_counter_copy()
 *
 * Frees a copied counter.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profiler_counter_free (LrgProfilerCounter *self);

/* ==========================================================================
 * Profiler Sample (Boxed Type, 0.8 statistics)
 * ========================================================================== */

#define LRG_TYPE_PROFILER_SAMPLE (lrg_profiler_sample_get_type ())

/**
 * LrgProfilerSample:
 *
 * A single timing sample from a profiler section.
 */
typedef struct _LrgProfilerSample LrgProfilerSample;

/**
 * lrg_profiler_sample_get_type:
 *
 * Gets the boxed #GType of #LrgProfilerSample.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType               lrg_profiler_sample_get_type      (void) G_GNUC_CONST;

/**
 * lrg_profiler_sample_copy:
 * @self: (nullable): a #LrgProfilerSample
 *
 * Copies a sample.
 *
 * Returns: (transfer full) (nullable): a copy, or %NULL for %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerSample * lrg_profiler_sample_copy          (const LrgProfilerSample *self);

/**
 * lrg_profiler_sample_free:
 * @self: (nullable): a #LrgProfilerSample
 *
 * Frees a sample.
 */
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
 * LrgProfilerCapture
 * ========================================================================== */

#define LRG_TYPE_PROFILER_CAPTURE (lrg_profiler_capture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProfilerCapture, lrg_profiler_capture, LRG, PROFILER_CAPTURE, GObject)

/**
 * lrg_profiler_capture_new:
 *
 * Creates an empty capture (no frames, no threads).
 *
 * Returns: (transfer full): a new #LrgProfilerCapture
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCapture * lrg_profiler_capture_new (void);

/**
 * lrg_profiler_capture_new_from_file:
 * @path: a `.rprof` file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a capture saved by lrg_profiler_capture_save_to_file().
 *
 * Returns: (transfer full) (nullable): the capture, or %NULL with @error
 *   set (#LRG_PROFILER_ERROR)
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCapture * lrg_profiler_capture_new_from_file (const gchar  *path,
                                                         GError      **error);

/**
 * lrg_profiler_capture_new_from_data:
 * @data: (array length=length) (element-type guint8): `.rprof` YAML text
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @error: return location for a #GError, or %NULL
 *
 * Parses a capture from memory.
 *
 * Returns: (transfer full) (nullable): the capture, or %NULL with @error
 *   set (#LRG_PROFILER_ERROR)
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCapture * lrg_profiler_capture_new_from_data (const gchar  *data,
                                                         gssize        length,
                                                         GError      **error);

/**
 * lrg_profiler_capture_save_to_data:
 * @self: an #LrgProfilerCapture
 * @length: (out) (optional): length of the result in bytes
 *
 * Serializes the capture as `.rprof` YAML.  The output is byte-stable:
 * saving a loaded capture reproduces the file.
 *
 * Returns: (transfer full): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_profiler_capture_save_to_data (LrgProfilerCapture *self,
                                           gsize              *length);

/**
 * lrg_profiler_capture_save_to_file:
 * @self: an #LrgProfilerCapture
 * @path: the file to write (conventionally `*.rprof`)
 * @error: return location for a #GError, or %NULL
 *
 * Writes the capture to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profiler_capture_save_to_file (LrgProfilerCapture  *self,
                                            const gchar         *path,
                                            GError             **error);

/**
 * lrg_profiler_capture_get_n_frames:
 * @self: an #LrgProfilerCapture
 *
 * Gets the number of frames, oldest first.  Frame accessors take a
 * position 0 .. n-1.
 *
 * Returns: the frame count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profiler_capture_get_n_frames (LrgProfilerCapture *self);

/**
 * lrg_profiler_capture_find_frame:
 * @self: an #LrgProfilerCapture
 * @index: a frame number (#LrgProfiler's monotonic frame index)
 *
 * Finds the position of the frame numbered @index.
 *
 * Returns: its position, or -1 when the capture has no such frame
 */
LRG_AVAILABLE_IN_ALL
gint lrg_profiler_capture_find_frame (LrgProfilerCapture *self,
                                      guint64             index);

/**
 * lrg_profiler_capture_get_frame_index:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 *
 * Gets the frame number of the frame at @position.
 *
 * Returns: the frame number
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_profiler_capture_get_frame_index (LrgProfilerCapture *self,
                                              guint               position);

/**
 * lrg_profiler_capture_get_frame_start_us:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 *
 * Gets when the frame began, relative to the first frame's start.
 *
 * Returns: microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_profiler_capture_get_frame_start_us (LrgProfilerCapture *self,
                                                guint               position);

/**
 * lrg_profiler_capture_get_frame_duration_us:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 *
 * Gets the frame's duration (begin_frame to end_frame).
 *
 * Returns: microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_profiler_capture_get_frame_duration_us (LrgProfilerCapture *self,
                                                   guint               position);

/**
 * lrg_profiler_capture_get_frame_gpu_us:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 *
 * Gets the GPU time of the frame: the sum of its top-level GPU events.
 *
 * Returns: microseconds, or -1 when the frame has no GPU events
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_profiler_capture_get_frame_gpu_us (LrgProfilerCapture *self,
                                              guint               position);

/**
 * lrg_profiler_capture_get_events:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 * @n_events: (out): the number of events
 *
 * Gets the CPU events of a frame, in begin order (parents first).
 *
 * Returns: (array length=n_events) (transfer none): the events, owned by
 *   the capture
 */
LRG_AVAILABLE_IN_ALL
const LrgProfilerEvent * lrg_profiler_capture_get_events (LrgProfilerCapture *self,
                                                          guint               position,
                                                          guint              *n_events);

/**
 * lrg_profiler_capture_get_gpu_events:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 * @n_events: (out): the number of events
 *
 * Gets the GPU events of a frame (thread %LRG_PROFILER_GPU_THREAD).
 *
 * Returns: (array length=n_events) (transfer none): the events, owned by
 *   the capture
 */
LRG_AVAILABLE_IN_ALL
const LrgProfilerEvent * lrg_profiler_capture_get_gpu_events (LrgProfilerCapture *self,
                                                              guint               position,
                                                              guint              *n_events);

/**
 * lrg_profiler_capture_get_counters:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 * @n_counters: (out): the number of counters
 *
 * Gets the counters of a frame, in the order they were first set.
 *
 * Returns: (array length=n_counters) (transfer none): the counters, owned
 *   by the capture
 */
LRG_AVAILABLE_IN_ALL
const LrgProfilerCounter * lrg_profiler_capture_get_counters (LrgProfilerCapture *self,
                                                              guint               position,
                                                              guint              *n_counters);

/**
 * lrg_profiler_capture_get_n_threads:
 * @self: an #LrgProfilerCapture
 *
 * Gets the number of CPU lanes (threads) events refer to.
 *
 * Returns: the thread count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profiler_capture_get_n_threads (LrgProfilerCapture *self);

/**
 * lrg_profiler_capture_get_thread_name:
 * @self: an #LrgProfilerCapture
 * @thread: a thread index, or %LRG_PROFILER_GPU_THREAD
 *
 * Gets a lane's name ("main", a name set with
 * lrg_profiler_set_thread_name(), "Thread N", or "GPU").
 *
 * Returns: (transfer none) (nullable): the name, or %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_profiler_capture_get_thread_name (LrgProfilerCapture *self,
                                                    guint               thread);

/**
 * lrg_profiler_capture_get_section_total_us:
 * @self: an #LrgProfilerCapture
 * @position: a frame position
 * @name: a section name
 * @calls: (out) (optional): how many events named @name the frame has
 *
 * Sums the inclusive time of the events named @name in a frame (CPU
 * lanes).  A section nested inside another of the same name is counted
 * once, through its outermost occurrence.
 *
 * Returns: microseconds (0 when absent)
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_profiler_capture_get_section_total_us (LrgProfilerCapture *self,
                                                  guint               position,
                                                  const gchar        *name,
                                                  guint              *calls);

/**
 * lrg_profiler_capture_get_section_names:
 * @self: an #LrgProfilerCapture
 * @position: a frame position, or -1 for every frame
 *
 * Gets the distinct CPU section names of one frame (or of all), sorted.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_profiler_capture_get_section_names (LrgProfilerCapture *self,
                                                 gint                position);

/* ==========================================================================
 * LrgProfiler: construction and singleton
 * ========================================================================== */

#define LRG_TYPE_PROFILER (lrg_profiler_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProfiler, lrg_profiler, LRG, PROFILER, GObject)

/**
 * lrg_profiler_get_default:
 *
 * Gets the default profiler instance (the one the engine, the renderer
 * and the editor time into).
 *
 * Returns: (transfer none): the default #LrgProfiler
 */
LRG_AVAILABLE_IN_ALL
LrgProfiler *       lrg_profiler_get_default          (void);

/**
 * lrg_profiler_new:
 *
 * Creates a new profiler.  It starts disabled.
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
 * Enables or disables profiling.  Toggle it between frames: a section
 * begun while disabled and ended while enabled is reported as unmatched.
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
 * @max_samples: maximum samples per section (at least 1)
 *
 * Sets the maximum number of samples to keep per section.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_max_samples      (LrgProfiler *self,
                                                       guint        max_samples);

/**
 * lrg_profiler_get_frame_capacity:
 * @self: a #LrgProfiler
 *
 * Gets how many finished frames the ring keeps.
 *
 * Returns: the ring size
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_profiler_get_frame_capacity   (LrgProfiler *self);

/**
 * lrg_profiler_set_frame_capacity:
 * @self: a #LrgProfiler
 * @capacity: frames to keep (clamped to 1 .. 65536)
 *
 * Sets the ring size.  Shrinking drops the oldest frames.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_frame_capacity   (LrgProfiler *self,
                                                       guint        capacity);

/**
 * lrg_profiler_set_thread_name:
 * @self: a #LrgProfiler
 * @name: the lane name for the calling thread
 *
 * Names the calling thread's lane.  Lanes are created on a thread's
 * first section; the thread that created the profiler is "main", others
 * default to "Thread N".
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_thread_name      (LrgProfiler *self,
                                                       const gchar *name);

/* ==========================================================================
 * Section Timing
 * ========================================================================== */

/**
 * lrg_profiler_begin_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Begins timing a section on the calling thread's lane.  A section begun
 * while another is open on the same thread is its child.  Must be paired
 * with lrg_profiler_end_section().  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_begin_section        (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_end_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Ends the innermost open section named @name on the calling thread (if
 * the thread has none, the innermost one on any thread).  Sections opened
 * inside it and still open keep running.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_end_section          (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_is_section_active:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Checks whether @name has been begun with lrg_profiler_begin_section()
 * and not yet ended, on any thread. Use it before lrg_profiler_end_section()
 * when the caller cannot be sure the section is open.
 *
 * Returns: %TRUE if the section is currently being timed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_profiler_is_section_active    (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_get_active_section_names:
 * @self: a #LrgProfiler
 *
 * Gets the names of all sections currently being timed (every thread,
 * outermost first, each name once).
 *
 * Returns: (transfer container) (element-type utf8): list of open section
 *   names; the strings are interned and never freed
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_profiler_get_active_section_names (LrgProfiler *self);

/**
 * lrg_profiler_begin_frame:
 * @self: a #LrgProfiler
 *
 * Marks the beginning of a frame.  Sections recorded since the last
 * lrg_profiler_end_frame() stay in the new frame.  With a GPU timer,
 * this also collects the GPU results of earlier frames.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_begin_frame          (LrgProfiler *self);

/**
 * lrg_profiler_end_frame:
 * @self: a #LrgProfiler
 *
 * Marks the end of a frame and moves it into the ring.  Sections still
 * open are cut at the frame end and continue in the next frame.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_end_frame            (LrgProfiler *self);

/**
 * lrg_profiler_get_frame_index:
 * @self: a #LrgProfiler
 *
 * Gets the number of the frame being recorded (frames are numbered from 0
 * and every lrg_profiler_end_frame() advances it).
 *
 * Returns: the current frame number
 */
LRG_AVAILABLE_IN_ALL
guint64             lrg_profiler_get_frame_index      (LrgProfiler *self);

/**
 * lrg_profiler_get_n_frames:
 * @self: a #LrgProfiler
 *
 * Gets the number of finished frames in the ring.
 *
 * Returns: the frame count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_profiler_get_n_frames         (LrgProfiler *self);

/**
 * lrg_profiler_set_counter:
 * @self: a #LrgProfiler
 * @name: the counter name
 * @value: its value for the current frame
 *
 * Sets a per-frame counter (the last value set in a frame wins).
 * Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_counter          (LrgProfiler *self,
                                                       const gchar *name,
                                                       gdouble      value);

/**
 * lrg_profiler_capture:
 * @self: a #LrgProfiler
 * @n_frames: how many of the newest finished frames to copy (0 = all)
 *
 * Copies frames out of the ring.  The profiler keeps recording.
 *
 * Returns: (transfer full): a new #LrgProfilerCapture
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCapture * lrg_profiler_capture             (LrgProfiler *self,
                                                       guint        n_frames);

/* ==========================================================================
 * GPU lane
 * ========================================================================== */

/**
 * lrg_profiler_get_gpu_enabled:
 * @self: a #LrgProfiler
 *
 * Gets whether GPU sections are timed.
 *
 * Returns: %TRUE when the GPU lane is on
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_profiler_get_gpu_enabled      (LrgProfiler *self);

/**
 * lrg_profiler_set_gpu_enabled:
 * @self: a #LrgProfiler
 * @enabled: whether to time GPU sections
 *
 * Turns the GPU lane on or off.  When on (and the profiler is enabled),
 * lrg_profiler_begin_gpu_section() issues GL timestamp queries through
 * the profiler's #LrgGpuTimer, created on first use (it needs a current
 * GL context; without one GPU sections are ignored).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_set_gpu_enabled      (LrgProfiler *self,
                                                       gboolean     enabled);

/**
 * lrg_profiler_get_gpu_timer:
 * @self: a #LrgProfiler
 *
 * Gets the profiler's GPU timer, if it was created.
 *
 * Returns: (transfer none) (nullable): the #LrgGpuTimer, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgGpuTimer *       lrg_profiler_get_gpu_timer        (LrgProfiler *self);

/**
 * lrg_profiler_begin_gpu_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Begins a GPU section (render thread only).  No-op unless both the
 * profiler and its GPU lane are enabled.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_begin_gpu_section    (LrgProfiler *self,
                                                       const gchar *name);

/**
 * lrg_profiler_end_gpu_section:
 * @self: a #LrgProfiler
 *
 * Ends the innermost open GPU section.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_end_gpu_section      (LrgProfiler *self);

/**
 * lrg_profiler_add_gpu_event:
 * @self: a #LrgProfiler
 * @frame_index: the frame the GPU work was submitted in
 * @name: the section name
 * @depth: nesting depth
 * @start_us: start, in the monotonic clock's microseconds
 *   (g_get_monotonic_time() base)
 * @duration_us: duration in microseconds
 *
 * Adds a finished GPU event to frame @frame_index (if it is still in the
 * ring or being recorded).  #LrgGpuTimer calls this once query results
 * arrive, usually two frames later; other GPU sources may too.
 *
 * Returns: %TRUE when the frame was found
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_profiler_add_gpu_event        (LrgProfiler *self,
                                                       guint64      frame_index,
                                                       const gchar *name,
                                                       guint        depth,
                                                       gint64       start_us,
                                                       gint64       duration_us);

/* ==========================================================================
 * Statistics (per section, over the last max-samples calls)
 * ========================================================================== */

/**
 * lrg_profiler_get_section_names:
 * @self: a #LrgProfiler
 *
 * Gets all section names that have been profiled.
 *
 * Returns: (transfer container) (element-type utf8): list of section
 *   names; the strings are interned and never freed
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
 * Clears all profiling data: statistics, open sections and the frame ring
 * (the frame number keeps counting).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_clear                (LrgProfiler *self);

/**
 * lrg_profiler_clear_section:
 * @self: a #LrgProfiler
 * @name: the section name
 *
 * Clears the statistics of one section and closes it if open.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_profiler_clear_section        (LrgProfiler *self,
                                                       const gchar *name);

G_END_DECLS
