/* lrg-profiler.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The profiler: hierarchical sections per thread lane, frames in a ring,
 * counters, a GPU lane and `.rprof` captures.
 *
 * Data model
 * ----------
 * Every thread that records gets a Lane holding its stack of open
 * sections.  The frame being recorded ("cur", created lazily) owns an
 * array of LrgProfilerEvent with ABSOLUTE monotonic start times; an event
 * is appended when its section begins (so parents precede children and
 * the parent index is known) and its duration is filled in when it ends.
 * lrg_profiler_end_frame() cuts the sections still open at the frame end
 * and marks their stack entries "carried": the next frame re-opens them
 * as continuation events starting at its own start.  Finished frames go
 * into a ring of frame-capacity entries.  lrg_profiler_capture() copies
 * frames out with times made relative (events to their frame, frames to
 * the first one), which is also what a `.rprof` file stores.
 *
 * The 0.8 statistics (last/avg/min/max over the last max-samples calls
 * per section name) are kept next to the frames, keyed by interned name.
 *
 * Every public entry point that records returns after one flag test
 * while the profiler is disabled; when enabled, recording takes one
 * mutex so worker threads can time their own lanes.
 */

#include "config.h"

#include <string.h>
#include <yaml-glib.h>

#include "lrg-profiler.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * Errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-profiler-error-quark, lrg_profiler_error)

GType
lrg_profiler_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PROFILER_ERROR_IO, "LRG_PROFILER_ERROR_IO", "io" },
			{ LRG_PROFILER_ERROR_PARSE, "LRG_PROFILER_ERROR_PARSE", "parse" },
			{ LRG_PROFILER_ERROR_INVALID, "LRG_PROFILER_ERROR_INVALID", "invalid" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgProfilerError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Boxed events, counters, samples
 * ========================================================================== */

LrgProfilerEvent *
lrg_profiler_event_copy (const LrgProfilerEvent *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return g_memdup2 (self, sizeof *self);
}

void
lrg_profiler_event_free (LrgProfilerEvent *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgProfilerEvent, lrg_profiler_event, lrg_profiler_event_copy, lrg_profiler_event_free)

LrgProfilerCounter *
lrg_profiler_counter_copy (const LrgProfilerCounter *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return g_memdup2 (self, sizeof *self);
}

void
lrg_profiler_counter_free (LrgProfilerCounter *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgProfilerCounter, lrg_profiler_counter, lrg_profiler_counter_copy, lrg_profiler_counter_free)

struct _LrgProfilerSample
{
	gchar  *name;
	gint64  duration_us;
};

static LrgProfilerSample *
sample_new (const gchar *name,
            gint64       duration_us)
{
	LrgProfilerSample *sample = g_new0 (LrgProfilerSample, 1);

	sample->name = g_strdup (name);
	sample->duration_us = duration_us;
	return sample;
}

LrgProfilerSample *
lrg_profiler_sample_copy (const LrgProfilerSample *self)
{
	if (self == NULL)
		return NULL;
	return sample_new (self->name, self->duration_us);
}

void
lrg_profiler_sample_free (LrgProfilerSample *self)
{
	if (self == NULL)
		return;
	g_free (self->name);
	g_free (self);
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
	return (gdouble) self->duration_us / 1000.0;
}

G_DEFINE_BOXED_TYPE (LrgProfilerSample, lrg_profiler_sample, lrg_profiler_sample_copy, lrg_profiler_sample_free)

/* ==========================================================================
 * Frames (shared by the profiler's ring and captures)
 * ========================================================================== */

typedef struct
{
	guint64  index;
	gint64   start_us;      /* profiler: absolute; capture: relative to frame 0 */
	gint64   end_us;        /* profiler only */
	gint64   duration_us;   /* capture only */
	gint64   gpu_us;        /* capture only, -1 = none */
	GArray  *events;        /* LrgProfilerEvent */
	GArray  *gpu;           /* LrgProfilerEvent, thread = LRG_PROFILER_GPU_THREAD */
	GArray  *counters;      /* LrgProfilerCounter */
} Frame;

static Frame *
frame_new (guint64 index)
{
	Frame *f = g_new0 (Frame, 1);

	f->index = index;
	f->gpu_us = -1;
	f->events = g_array_new (FALSE, FALSE, sizeof (LrgProfilerEvent));
	f->gpu = g_array_new (FALSE, FALSE, sizeof (LrgProfilerEvent));
	f->counters = g_array_new (FALSE, FALSE, sizeof (LrgProfilerCounter));
	return f;
}

static void
frame_free (gpointer data)
{
	Frame *f = data;

	if (f == NULL)
		return;
	g_array_unref (f->events);
	g_array_unref (f->gpu);
	g_array_unref (f->counters);
	g_free (f);
}

/* Sets (or adds) counter @name (interned) in @f. */
static void
frame_set_counter (Frame       *f,
                   const gchar *name,
                   gdouble      value)
{
	LrgProfilerCounter c;
	guint              i;

	for (i = 0; i < f->counters->len; i++)
		if (g_array_index (f->counters, LrgProfilerCounter, i).name == name)
		{
			g_array_index (f->counters, LrgProfilerCounter, i).value = value;
			return;
		}
	c.name = name;
	c.value = value;
	g_array_append_val (f->counters, c);
}

/* Appends a GPU event, finding its parent: the last earlier event one
 * level up (the timer reports sections in begin order). */
static void
frame_add_gpu (Frame       *f,
               const gchar *name,
               guint        depth,
               gint64       start_us,
               gint64       duration_us)
{
	LrgProfilerEvent e;
	gint             i;

	e.name = name;
	e.thread = LRG_PROFILER_GPU_THREAD;
	e.depth = depth;
	e.parent = -1;
	e.start_us = start_us;
	e.duration_us = duration_us;
	if (depth > 0)
		for (i = (gint) f->gpu->len - 1; i >= 0; i--)
			if (g_array_index (f->gpu, LrgProfilerEvent, i).depth + 1 == depth)
			{
				e.parent = i;
				break;
			}
	g_array_append_val (f->gpu, e);
}

/* ==========================================================================
 * LrgProfilerCapture
 * ========================================================================== */

struct _LrgProfilerCapture
{
	GObject    parent_instance;

	GPtrArray *frames;    /* Frame, relative times */
	GPtrArray *threads;   /* gchar* */
};

G_DEFINE_FINAL_TYPE (LrgProfilerCapture, lrg_profiler_capture, G_TYPE_OBJECT)

static void
lrg_profiler_capture_finalize (GObject *object)
{
	LrgProfilerCapture *self = LRG_PROFILER_CAPTURE (object);

	g_ptr_array_unref (self->frames);
	g_ptr_array_unref (self->threads);
	G_OBJECT_CLASS (lrg_profiler_capture_parent_class)->finalize (object);
}

static void
lrg_profiler_capture_class_init (LrgProfilerCaptureClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_profiler_capture_finalize;
}

static void
lrg_profiler_capture_init (LrgProfilerCapture *self)
{
	self->frames = g_ptr_array_new_with_free_func (frame_free);
	self->threads = g_ptr_array_new_with_free_func (g_free);
}

LrgProfilerCapture *
lrg_profiler_capture_new (void)
{
	return g_object_new (LRG_TYPE_PROFILER_CAPTURE, NULL);
}

/* The capture's frame at @position (a critical and NULL when out of range). */
static Frame *
cap_frame (LrgProfilerCapture *self,
           guint               position)
{
	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), NULL);
	g_return_val_if_fail (position < self->frames->len, NULL);
	return g_ptr_array_index (self->frames, position);
}

guint
lrg_profiler_capture_get_n_frames (LrgProfilerCapture *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), 0);
	return self->frames->len;
}

gint
lrg_profiler_capture_find_frame (LrgProfilerCapture *self,
                                 guint64             index)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), -1);
	for (i = 0; i < self->frames->len; i++)
		if (((Frame *) g_ptr_array_index (self->frames, i))->index == index)
			return (gint) i;
	return -1;
}

guint64
lrg_profiler_capture_get_frame_index (LrgProfilerCapture *self,
                                      guint               position)
{
	Frame *f = cap_frame (self, position);

	return f != NULL ? f->index : 0;
}

gint64
lrg_profiler_capture_get_frame_start_us (LrgProfilerCapture *self,
                                         guint               position)
{
	Frame *f = cap_frame (self, position);

	return f != NULL ? f->start_us : 0;
}

gint64
lrg_profiler_capture_get_frame_duration_us (LrgProfilerCapture *self,
                                            guint               position)
{
	Frame *f = cap_frame (self, position);

	return f != NULL ? f->duration_us : 0;
}

gint64
lrg_profiler_capture_get_frame_gpu_us (LrgProfilerCapture *self,
                                       guint               position)
{
	Frame *f = cap_frame (self, position);

	return f != NULL ? f->gpu_us : -1;
}

const LrgProfilerEvent *
lrg_profiler_capture_get_events (LrgProfilerCapture *self,
                                 guint               position,
                                 guint              *n_events)
{
	Frame *f = cap_frame (self, position);

	g_return_val_if_fail (n_events != NULL, NULL);
	*n_events = (f != NULL) ? f->events->len : 0;
	return (f != NULL) ? (const LrgProfilerEvent *) f->events->data : NULL;
}

const LrgProfilerEvent *
lrg_profiler_capture_get_gpu_events (LrgProfilerCapture *self,
                                     guint               position,
                                     guint              *n_events)
{
	Frame *f = cap_frame (self, position);

	g_return_val_if_fail (n_events != NULL, NULL);
	*n_events = (f != NULL) ? f->gpu->len : 0;
	return (f != NULL) ? (const LrgProfilerEvent *) f->gpu->data : NULL;
}

const LrgProfilerCounter *
lrg_profiler_capture_get_counters (LrgProfilerCapture *self,
                                   guint               position,
                                   guint              *n_counters)
{
	Frame *f = cap_frame (self, position);

	g_return_val_if_fail (n_counters != NULL, NULL);
	*n_counters = (f != NULL) ? f->counters->len : 0;
	return (f != NULL) ? (const LrgProfilerCounter *) f->counters->data : NULL;
}

guint
lrg_profiler_capture_get_n_threads (LrgProfilerCapture *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), 0);
	return self->threads->len;
}

const gchar *
lrg_profiler_capture_get_thread_name (LrgProfilerCapture *self,
                                      guint               thread)
{
	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), NULL);

	if (thread == LRG_PROFILER_GPU_THREAD)
		return "GPU";
	if (thread >= self->threads->len)
		return NULL;
	return g_ptr_array_index (self->threads, thread);
}

/*
 * Inclusive time of the events named @name: an event counts unless one of
 * its ancestors has the same name (recursion is counted once).
 */
gint64
lrg_profiler_capture_get_section_total_us (LrgProfilerCapture *self,
                                           guint               position,
                                           const gchar        *name,
                                           guint              *calls)
{
	Frame       *f = cap_frame (self, position);
	const gchar *iname;
	gint64       total = 0;
	guint        n = 0, i;

	if (calls != NULL)
		*calls = 0;
	g_return_val_if_fail (name != NULL, 0);
	if (f == NULL)
		return 0;

	iname = g_intern_string (name);
	for (i = 0; i < f->events->len; i++)
	{
		const LrgProfilerEvent *e = &g_array_index (f->events, LrgProfilerEvent, i);
		gint                    p;
		gboolean                nested = FALSE;

		if (e->name != iname)
			continue;
		n++;
		for (p = e->parent; p >= 0; p = g_array_index (f->events, LrgProfilerEvent, p).parent)
			if (g_array_index (f->events, LrgProfilerEvent, p).name == iname)
			{
				nested = TRUE;
				break;
			}
		if (!nested)
			total += e->duration_us;
	}
	if (calls != NULL)
		*calls = n;
	return total;
}

gchar **
lrg_profiler_capture_get_section_names (LrgProfilerCapture *self,
                                        gint                position)
{
	g_autoptr(GHashTable) seen = NULL;
	GPtrArray            *out;
	guint                 first, last, i, k;

	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), NULL);

	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	out = g_ptr_array_new ();
	if (position >= 0)
	{
		g_return_val_if_fail ((guint) position < self->frames->len, (gchar **) g_ptr_array_free (out, FALSE));
		first = (guint) position;
		last = first + 1;
	}
	else
	{
		first = 0;
		last = self->frames->len;
	}
	for (k = first; k < last; k++)
	{
		Frame *f = g_ptr_array_index (self->frames, k);

		for (i = 0; i < f->events->len; i++)
		{
			const gchar *name = g_array_index (f->events, LrgProfilerEvent, i).name;

			if (g_hash_table_add (seen, (gpointer) name))
				g_ptr_array_add (out, g_strdup (name));
		}
	}
	g_ptr_array_sort_values (out, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------
 * .rprof writing
 * -------------------------------------------------------------------------- */

/* A double-quoted YAML scalar (names may hold any character). */
static void
put_quoted (GString     *out,
            const gchar *s)
{
	const guchar *p;

	g_string_append_c (out, '"');
	for (p = (const guchar *) s; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '"':
			g_string_append (out, "\\\"");
			break;
		case '\\':
			g_string_append (out, "\\\\");
			break;
		case '\n':
			g_string_append (out, "\\n");
			break;
		case '\t':
			g_string_append (out, "\\t");
			break;
		default:
			if (*p < 0x20)
				g_string_append_printf (out, "\\x%02x", (guint) *p);
			else
				g_string_append_c (out, (gchar) *p);
			break;
		}
	}
	g_string_append_c (out, '"');
}

/* The shortest of %.15g / %.17g that reads back as @v (stable output). */
static void
put_double (GString *out,
            gdouble  v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.15g", v);
	if (g_ascii_strtod (buf, NULL) != v)
		g_ascii_formatd (buf, sizeof buf, "%.17g", v);
	g_string_append (out, buf);
}

gchar *
lrg_profiler_capture_save_to_data (LrgProfilerCapture *self,
                                   gsize              *length)
{
	GString *out;
	guint    i, k;

	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), NULL);

	out = g_string_new ("# libregnum profiler capture: events are [name, thread, depth, parent, start_us, duration_us]\n");
	g_string_append (out, "format: rprof\nversion: 1\nthreads:\n");
	for (i = 0; i < self->threads->len; i++)
	{
		g_string_append (out, "  - ");
		put_quoted (out, g_ptr_array_index (self->threads, i));
		g_string_append_c (out, '\n');
	}
	if (self->frames->len == 0)
		g_string_append (out, "frames: []\n");
	else
		g_string_append (out, "frames:\n");
	for (k = 0; k < self->frames->len; k++)
	{
		Frame *f = g_ptr_array_index (self->frames, k);

		g_string_append_printf (out, "  - index: %" G_GUINT64_FORMAT "\n", f->index);
		g_string_append_printf (out, "    start_us: %" G_GINT64_FORMAT "\n", f->start_us);
		g_string_append_printf (out, "    duration_us: %" G_GINT64_FORMAT "\n", f->duration_us);
		if (f->events->len > 0)
		{
			g_string_append (out, "    events:\n");
			for (i = 0; i < f->events->len; i++)
			{
				const LrgProfilerEvent *e = &g_array_index (f->events, LrgProfilerEvent, i);

				g_string_append (out, "      - [");
				put_quoted (out, e->name);
				g_string_append_printf (out, ", %u, %u, %d, %" G_GINT64_FORMAT ", %" G_GINT64_FORMAT "]\n",
				                        e->thread, e->depth, e->parent, e->start_us, e->duration_us);
			}
		}
		if (f->gpu->len > 0)
		{
			g_string_append (out, "    gpu:\n");
			for (i = 0; i < f->gpu->len; i++)
			{
				const LrgProfilerEvent *e = &g_array_index (f->gpu, LrgProfilerEvent, i);

				g_string_append (out, "      - [");
				put_quoted (out, e->name);
				g_string_append_printf (out, ", %u, %d, %" G_GINT64_FORMAT ", %" G_GINT64_FORMAT "]\n",
				                        e->depth, e->parent, e->start_us, e->duration_us);
			}
		}
		if (f->counters->len > 0)
		{
			g_string_append (out, "    counters:\n");
			for (i = 0; i < f->counters->len; i++)
			{
				const LrgProfilerCounter *c = &g_array_index (f->counters, LrgProfilerCounter, i);

				g_string_append (out, "      ");
				put_quoted (out, c->name);
				g_string_append (out, ": ");
				put_double (out, c->value);
				g_string_append_c (out, '\n');
			}
		}
	}
	if (length != NULL)
		*length = out->len;
	return g_string_free (out, FALSE);
}

gboolean
lrg_profiler_capture_save_to_file (LrgProfilerCapture  *self,
                                   const gchar         *path,
                                   GError             **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;
	GError           *local = NULL;

	g_return_val_if_fail (LRG_IS_PROFILER_CAPTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = lrg_profiler_capture_save_to_data (self, &length);
	if (!g_file_set_contents (path, data, (gssize) length, &local))
	{
		g_set_error (error, LRG_PROFILER_ERROR, LRG_PROFILER_ERROR_IO, "%s", local->message);
		g_error_free (local);
		return FALSE;
	}
	return TRUE;
}

/* --------------------------------------------------------------------------
 * .rprof reading
 * -------------------------------------------------------------------------- */

static gboolean
invalid (GError      **error,
         const gchar  *fmt,
         ...) G_GNUC_PRINTF (2, 3);

static gboolean
invalid (GError      **error,
         const gchar  *fmt,
         ...)
{
	va_list           ap;
	g_autofree gchar *msg = NULL;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	g_set_error (error, LRG_PROFILER_ERROR, LRG_PROFILER_ERROR_INVALID, "%s", msg);
	return FALSE;
}

/* A scalar node as a string, or NULL. */
static const gchar *
node_scalar (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* A scalar integer node (strict: digits only, optional sign). */
static gboolean
node_int (YamlNode *node,
          gint64   *out)
{
	const gchar *s = node_scalar (node);

	return s != NULL && g_ascii_string_to_signed (s, 10, G_MININT64, G_MAXINT64, out, NULL);
}

static gboolean
node_double (YamlNode *node,
             gdouble  *out)
{
	const gchar *s = node_scalar (node);
	gchar       *end = NULL;

	if (s == NULL || *s == '\0')
		return FALSE;
	*out = g_ascii_strtod (s, &end);
	return end != NULL && *end == '\0';
}

/* An event tuple: [name, thread, depth, parent, start, duration] (CPU) or
 * [name, depth, parent, start, duration] (GPU). */
static gboolean
parse_event (YamlNode          *node,
             gboolean           gpu,
             guint              own_index,
             guint              n_threads,
             LrgProfilerEvent  *e,
             const gchar       *where,
             GError           **error)
{
	YamlSequence *seq;
	gint64        v[5];
	guint         n_num = gpu ? 4 : 5, i;
	const gchar  *name;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE ||
	    yaml_sequence_get_length (seq = yaml_node_get_sequence (node)) != n_num + 1)
		return invalid (error, "%s: expected [name, %sdepth, parent, start_us, duration_us]", where,
		                gpu ? "" : "thread, ");
	name = node_scalar (yaml_sequence_get_element (seq, 0));
	if (name == NULL)
		return invalid (error, "%s[0]: the name must be a string", where);
	for (i = 0; i < n_num; i++)
		if (!node_int (yaml_sequence_get_element (seq, i + 1), &v[i]))
			return invalid (error, "%s[%u]: expected an integer", where, i + 1);

	e->name = g_intern_string (name);
	if (gpu)
	{
		e->thread = LRG_PROFILER_GPU_THREAD;
		e->depth = (guint) MAX (v[0], 0);
		e->parent = (gint) v[1];
		e->start_us = v[2];
		e->duration_us = v[3];
		if (v[0] < 0)
			return invalid (error, "%s[1]: depth must not be negative", where);
	}
	else
	{
		if (v[0] < 0 || (guint64) v[0] >= n_threads)
			return invalid (error, "%s[1]: thread %" G_GINT64_FORMAT " is not in threads", where, v[0]);
		if (v[1] < 0)
			return invalid (error, "%s[2]: depth must not be negative", where);
		e->thread = (guint) v[0];
		e->depth = (guint) v[1];
		e->parent = (gint) v[2];
		e->start_us = v[3];
		e->duration_us = v[4];
	}
	if (e->parent < -1 || e->parent >= (gint) own_index)
		return invalid (error, "%s: parent %d must be -1 or an earlier event", where, e->parent);
	if (e->duration_us < 0)
		return invalid (error, "%s: duration_us must not be negative", where);
	return TRUE;
}

static gboolean
parse_frame (YamlNode     *node,
             guint         k,
             guint         n_threads,
             Frame       **out,
             GError      **error)
{
	YamlMapping      *map;
	g_autofree gchar *path = g_strdup_printf ("frames[%u]", k);
	Frame            *f;
	YamlNode         *n;
	gint64            v;
	guint             i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return invalid (error, "%s: expected a mapping", path);
	map = yaml_node_get_mapping (node);
	f = frame_new (0);

	if (!node_int (yaml_mapping_get_member (map, "index"), &v) || v < 0)
	{
		frame_free (f);
		return invalid (error, "%s.index: expected a non-negative integer", path);
	}
	f->index = (guint64) v;
	if (!node_int (yaml_mapping_get_member (map, "start_us"), &f->start_us))
	{
		frame_free (f);
		return invalid (error, "%s.start_us: expected an integer", path);
	}
	if (!node_int (yaml_mapping_get_member (map, "duration_us"), &f->duration_us) || f->duration_us < 0)
	{
		frame_free (f);
		return invalid (error, "%s.duration_us: expected a non-negative integer", path);
	}

	n = yaml_mapping_get_member (map, "events");
	if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlSequence *seq;

		if (yaml_node_get_node_type (n) != YAML_NODE_SEQUENCE)
		{
			frame_free (f);
			return invalid (error, "%s.events: expected a sequence", path);
		}
		seq = yaml_node_get_sequence (n);
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *where = g_strdup_printf ("%s.events[%u]", path, i);
			LrgProfilerEvent  e;

			if (!parse_event (yaml_sequence_get_element (seq, i), FALSE, i, n_threads, &e, where, error))
			{
				frame_free (f);
				return FALSE;
			}
			g_array_append_val (f->events, e);
		}
	}

	n = yaml_mapping_get_member (map, "gpu");
	if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlSequence *seq;

		if (yaml_node_get_node_type (n) != YAML_NODE_SEQUENCE)
		{
			frame_free (f);
			return invalid (error, "%s.gpu: expected a sequence", path);
		}
		seq = yaml_node_get_sequence (n);
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *where = g_strdup_printf ("%s.gpu[%u]", path, i);
			LrgProfilerEvent  e;

			if (!parse_event (yaml_sequence_get_element (seq, i), TRUE, i, n_threads, &e, where, error))
			{
				frame_free (f);
				return FALSE;
			}
			g_array_append_val (f->gpu, e);
			if (e.depth == 0)
				f->gpu_us = MAX (f->gpu_us, 0) + e.duration_us;
		}
	}

	n = yaml_mapping_get_member (map, "counters");
	if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlMapping *cm;

		if (yaml_node_get_node_type (n) != YAML_NODE_MAPPING)
		{
			frame_free (f);
			return invalid (error, "%s.counters: expected a mapping", path);
		}
		cm = yaml_node_get_mapping (n);
		for (i = 0; i < yaml_mapping_get_size (cm); i++)
		{
			gdouble d;

			if (!node_double (yaml_mapping_get_value (cm, i), &d))
			{
				frame_free (f);
				return invalid (error, "%s.counters.%s: expected a number", path, yaml_mapping_get_key (cm, i));
			}
			frame_set_counter (f, g_intern_string (yaml_mapping_get_key (cm, i)), d);
		}
	}
	*out = f;
	return TRUE;
}

static LrgProfilerCapture *
capture_from_root (YamlNode  *root,
                   GError   **error)
{
	g_autoptr(LrgProfilerCapture) self = NULL;
	YamlMapping                  *map;
	YamlNode                     *n;
	const gchar                  *format;
	gint64                        version;
	guint                         i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		invalid (error, "the document must be a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);
	format = node_scalar (yaml_mapping_get_member (map, "format"));
	if (g_strcmp0 (format, "rprof") != 0)
	{
		invalid (error, "format: expected \"rprof\"");
		return NULL;
	}
	if (!node_int (yaml_mapping_get_member (map, "version"), &version) || version != 1)
	{
		invalid (error, "version: expected 1");
		return NULL;
	}

	self = lrg_profiler_capture_new ();
	n = yaml_mapping_get_member (map, "threads");
	if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (n);

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			const gchar *name = node_scalar (yaml_sequence_get_element (seq, i));

			if (name == NULL)
			{
				invalid (error, "threads[%u]: expected a string", i);
				return NULL;
			}
			g_ptr_array_add (self->threads, g_strdup (name));
		}
	}
	else if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		invalid (error, "threads: expected a sequence");
		return NULL;
	}

	n = yaml_mapping_get_member (map, "frames");
	if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (n);

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			Frame *f = NULL;

			if (!parse_frame (yaml_sequence_get_element (seq, i), i, self->threads->len, &f, error))
				return NULL;
			g_ptr_array_add (self->frames, f);
		}
	}
	else if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		invalid (error, "frames: expected a sequence");
		return NULL;
	}
	return g_steal_pointer (&self);
}

LrgProfilerCapture *
lrg_profiler_capture_new_from_data (const gchar  *data,
                                    gssize        length,
                                    GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	GError               *local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_PROFILER_ERROR, LRG_PROFILER_ERROR_PARSE, "Profiler capture YAML: %s",
		             local->message);
		g_error_free (local);
		return NULL;
	}
	return capture_from_root (yaml_parser_get_root (parser), error);
}

LrgProfilerCapture *
lrg_profiler_capture_new_from_file (const gchar  *path,
                                    GError      **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;
	GError           *local = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &data, &length, &local))
	{
		g_set_error (error, LRG_PROFILER_ERROR, LRG_PROFILER_ERROR_IO, "%s", local->message);
		g_error_free (local);
		return NULL;
	}
	return lrg_profiler_capture_new_from_data (data, (gssize) length, error);
}

/* ==========================================================================
 * Profiler: lanes and statistics
 * ========================================================================== */

typedef struct
{
	const gchar *name;      /* interned */
	gint64       start_us;  /* absolute */
	gint         event;     /* index in frame `frame`'s events, or -1 */
	guint64      frame;     /* frame the event lives in; G_MAXUINT64 = carried */
} OpenEntry;

typedef struct
{
	GThread *thread;
	gchar   *name;
	GArray  *stack;         /* OpenEntry, innermost last */
} Lane;

static void
lane_free (gpointer data)
{
	Lane *lane = data;

	g_array_unref (lane->stack);
	g_free (lane->name);
	g_free (lane);
}

typedef struct
{
	gint64 *ring;           /* max_samples durations */
	guint   head;           /* next write */
	guint   count;
	guint   capacity;
	gint64  total_us;       /* over the ring */
	gint64  min_us;
	gint64  max_us;
	gint64  last_us;
} SectionStats;

static SectionStats *
stats_new (guint capacity)
{
	SectionStats *s = g_new0 (SectionStats, 1);

	s->capacity = MAX (capacity, 1u);
	s->ring = g_new0 (gint64, s->capacity);
	s->min_us = G_MAXINT64;
	return s;
}

static void
stats_free (gpointer data)
{
	SectionStats *s = data;

	g_free (s->ring);
	g_free (s);
}

/* Adds a sample, dropping the oldest once the ring is full. */
static void
stats_add (SectionStats *s,
           gint64        us)
{
	if (s->count == s->capacity)
		s->total_us -= s->ring[s->head];
	else
		s->count++;
	s->ring[s->head] = us;
	s->head = (s->head + 1) % s->capacity;
	s->total_us += us;
	s->last_us = us;
	s->min_us = MIN (s->min_us, us);
	s->max_us = MAX (s->max_us, us);
}

/* Resizes the ring keeping the newest samples. */
static void
stats_resize (SectionStats *s,
              guint         capacity)
{
	gint64 *ring = g_new0 (gint64, capacity);
	guint   keep = MIN (s->count, capacity);
	guint   i;

	s->total_us = 0;
	for (i = 0; i < keep; i++)
	{
		guint src = (s->head + s->capacity - keep + i) % s->capacity;

		ring[i] = s->ring[src];
		s->total_us += ring[i];
	}
	g_free (s->ring);
	s->ring = ring;
	s->capacity = capacity;
	s->count = keep;
	s->head = keep % capacity;
}

/* ==========================================================================
 * Profiler instance
 * ========================================================================== */

struct _LrgProfiler
{
	GObject      parent_instance;

	GMutex       lock;
	gboolean     enabled;
	guint        max_samples;
	guint        capacity;
	gboolean     gpu_enabled;
	LrgGpuTimer *gpu_timer;

	GHashTable  *sections;      /* interned name -> SectionStats */
	GPtrArray   *lanes;         /* Lane */
	GThread     *owner;         /* the creating thread: lane "main" */
	Frame       *cur;           /* frame being recorded, or NULL */
	GQueue       ring;          /* Frame, oldest at head */
	guint64      frame_counter; /* number of cur / the next frame */
	gboolean     overflow_warned;

	gint64       frame_start;
	gint64       last_frame_time_us;
	gdouble      fps;
	guint        frame_count;
	gint64       fps_timer_start;
};

enum
{
	PROP_0,
	PROP_ENABLED,
	PROP_MAX_SAMPLES,
	PROP_FRAME_CAPACITY,
	PROP_GPU_ENABLED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgProfiler, lrg_profiler, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgProfiler *default_profiler = NULL;

static void
lrg_profiler_finalize (GObject *object)
{
	LrgProfiler *self = LRG_PROFILER (object);

	g_hash_table_destroy (self->sections);
	g_ptr_array_unref (self->lanes);
	frame_free (self->cur);
	g_queue_clear_full (&self->ring, frame_free);
	g_clear_object (&self->gpu_timer);
	g_mutex_clear (&self->lock);

	if (default_profiler == self)
		default_profiler = NULL;

	G_OBJECT_CLASS (lrg_profiler_parent_class)->finalize (object);
}

static void
lrg_profiler_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgProfiler *self = LRG_PROFILER (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	case PROP_MAX_SAMPLES:
		g_value_set_uint (value, self->max_samples);
		break;
	case PROP_FRAME_CAPACITY:
		g_value_set_uint (value, self->capacity);
		break;
	case PROP_GPU_ENABLED:
		g_value_set_boolean (value, self->gpu_enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_profiler_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgProfiler *self = LRG_PROFILER (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		lrg_profiler_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_MAX_SAMPLES:
		lrg_profiler_set_max_samples (self, g_value_get_uint (value));
		break;
	case PROP_FRAME_CAPACITY:
		lrg_profiler_set_frame_capacity (self, g_value_get_uint (value));
		break;
	case PROP_GPU_ENABLED:
		lrg_profiler_set_gpu_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_profiler_class_init (LrgProfilerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_profiler_finalize;
	object_class->get_property = lrg_profiler_get_property;
	object_class->set_property = lrg_profiler_set_property;

	/**
	 * LrgProfiler:enabled:
	 *
	 * Whether sections, frames and counters are recorded.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether the profiler records", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgProfiler:max-samples:
	 *
	 * Samples kept per section for the average (at least 1).
	 */
	properties[PROP_MAX_SAMPLES] =
		g_param_spec_uint ("max-samples", "Max samples", "Samples kept per section", 1, G_MAXUINT, 60,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgProfiler:frame-capacity:
	 *
	 * Finished frames kept in the ring.
	 */
	properties[PROP_FRAME_CAPACITY] =
		g_param_spec_uint ("frame-capacity", "Frame capacity", "Frames kept in the ring", 1, 65536,
		                   LRG_PROFILER_DEFAULT_FRAME_CAPACITY,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgProfiler:gpu-enabled:
	 *
	 * Whether GPU sections are timed with an #LrgGpuTimer.
	 */
	properties[PROP_GPU_ENABLED] =
		g_param_spec_boolean ("gpu-enabled", "GPU enabled", "Whether GPU sections are timed", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_profiler_init (LrgProfiler *self)
{
	g_mutex_init (&self->lock);
	self->max_samples = 60;
	self->capacity = LRG_PROFILER_DEFAULT_FRAME_CAPACITY;
	self->sections = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, stats_free);
	self->lanes = g_ptr_array_new_with_free_func (lane_free);
	self->owner = g_thread_self ();
	g_queue_init (&self->ring);
	self->fps_timer_start = g_get_monotonic_time ();
}

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

/* --------------------------------------------------------------------------
 * Internals (called with the lock held)
 * -------------------------------------------------------------------------- */

/* The calling thread's lane index, created on first use. */
static guint
lane_index (LrgProfiler *self)
{
	GThread *me = g_thread_self ();
	Lane    *lane;
	guint    i;

	for (i = 0; i < self->lanes->len; i++)
		if (((Lane *) g_ptr_array_index (self->lanes, i))->thread == me)
			return i;
	lane = g_new0 (Lane, 1);
	lane->thread = me;
	lane->name = (me == self->owner) ? g_strdup ("main") : g_strdup_printf ("Thread %u", self->lanes->len + 1);
	lane->stack = g_array_new (FALSE, FALSE, sizeof (OpenEntry));
	g_ptr_array_add (self->lanes, lane);
	return self->lanes->len - 1;
}

/* Appends a CPU event to @f (unless the frame is full): returns its index or -1. */
static gint
frame_add_event (LrgProfiler *self,
                 Frame       *f,
                 const gchar *name,
                 guint        thread,
                 guint        depth,
                 gint         parent,
                 gint64       start_us)
{
	LrgProfilerEvent e;

	if (f->events->len >= LRG_PROFILER_MAX_EVENTS_PER_FRAME)
	{
		if (!self->overflow_warned)
			lrg_warning (LRG_LOG_DOMAIN_DEBUG, "Profiler frame %" G_GUINT64_FORMAT " exceeds %d events: dropping",
			             f->index, LRG_PROFILER_MAX_EVENTS_PER_FRAME);
		self->overflow_warned = TRUE;
		return -1;
	}
	e.name = name;
	e.thread = thread;
	e.depth = depth;
	e.parent = parent;
	e.start_us = start_us;
	e.duration_us = -1;
	g_array_append_val (f->events, e);
	return (gint) f->events->len - 1;
}

/*
 * The frame being recorded, created when needed.  A new frame re-opens
 * the sections carried over from the last one (outermost first, so the
 * parent links hold) as continuation events at its start.
 */
static Frame *
ensure_frame (LrgProfiler *self,
              gint64       now)
{
	guint i, k;

	if (self->cur != NULL)
		return self->cur;
	self->cur = frame_new (self->frame_counter);
	self->cur->start_us = now;
	for (i = 0; i < self->lanes->len; i++)
	{
		Lane *lane = g_ptr_array_index (self->lanes, i);
		gint  parent = -1;

		for (k = 0; k < lane->stack->len; k++)
		{
			OpenEntry *o = &g_array_index (lane->stack, OpenEntry, k);

			o->event = frame_add_event (self, self->cur, o->name, i, k, parent, now);
			o->frame = self->cur->index;
			parent = o->event;
		}
	}
	return self->cur;
}

/* Statistics for interned @name, created when needed. */
static SectionStats *
stats_for (LrgProfiler *self,
           const gchar *name)
{
	SectionStats *s = g_hash_table_lookup (self->sections, name);

	if (s == NULL)
	{
		s = stats_new (self->max_samples);
		g_hash_table_insert (self->sections, (gpointer) name, s);
	}
	return s;
}

/* --------------------------------------------------------------------------
 * Control
 * -------------------------------------------------------------------------- */

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

	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Profiler %s", enabled ? "enabled" : "disabled");
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
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
	GHashTableIter iter;
	gpointer       value;

	g_return_if_fail (LRG_IS_PROFILER (self));

	max_samples = MAX (1u, max_samples);
	if (self->max_samples == max_samples)
		return;
	g_mutex_lock (&self->lock);
	self->max_samples = max_samples;
	g_hash_table_iter_init (&iter, self->sections);
	while (g_hash_table_iter_next (&iter, NULL, &value))
		stats_resize (value, max_samples);
	g_mutex_unlock (&self->lock);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_SAMPLES]);
}

guint
lrg_profiler_get_frame_capacity (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
	return self->capacity;
}

void
lrg_profiler_set_frame_capacity (LrgProfiler *self,
                                 guint        capacity)
{
	g_return_if_fail (LRG_IS_PROFILER (self));

	capacity = CLAMP (capacity, 1u, 65536u);
	if (self->capacity == capacity)
		return;
	g_mutex_lock (&self->lock);
	self->capacity = capacity;
	while (g_queue_get_length (&self->ring) > capacity)
		frame_free (g_queue_pop_head (&self->ring));
	g_mutex_unlock (&self->lock);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_CAPACITY]);
}

void
lrg_profiler_set_thread_name (LrgProfiler *self,
                              const gchar *name)
{
	Lane *lane;
	guint li;

	g_return_if_fail (LRG_IS_PROFILER (self));
	g_return_if_fail (name != NULL);

	/* lane_index() may grow the lane array.  g_ptr_array_index() reads
	 * pdata before its argument is evaluated, so the two must not be
	 * combined: the index would land in the array from before the grow. */
	g_mutex_lock (&self->lock);
	li = lane_index (self);
	lane = g_ptr_array_index (self->lanes, li);
	g_free (lane->name);
	lane->name = g_strdup (name);
	g_mutex_unlock (&self->lock);
}

/* --------------------------------------------------------------------------
 * Sections
 * -------------------------------------------------------------------------- */

void
lrg_profiler_begin_section (LrgProfiler *self,
                            const gchar *name)
{
	const gchar *iname;
	gint64       now;
	Lane        *lane;
	Frame       *f;
	OpenEntry    o;
	gint         parent = -1;
	guint        li;

	g_return_if_fail (LRG_IS_PROFILER (self));
	g_return_if_fail (name != NULL);

	if (G_LIKELY (!self->enabled))
		return;

	iname = g_intern_string (name);
	now = g_get_monotonic_time ();
	g_mutex_lock (&self->lock);
	li = lane_index (self);
	lane = g_ptr_array_index (self->lanes, li);
	f = ensure_frame (self, now);

	/* The parent is the innermost open section of this lane, when its
	 * event lives in this frame */
	if (lane->stack->len > 0)
	{
		OpenEntry *top = &g_array_index (lane->stack, OpenEntry, lane->stack->len - 1);

		if (top->frame == f->index)
			parent = top->event;
	}
	o.name = iname;
	o.start_us = now;
	o.event = frame_add_event (self, f, iname, li, lane->stack->len, parent, now);
	o.frame = f->index;
	g_array_append_val (lane->stack, o);
	g_mutex_unlock (&self->lock);
}

/* Finds the innermost open entry named @iname: the calling lane first,
 * then any lane.  Returns FALSE when none is open. */
static gboolean
find_open (LrgProfiler  *self,
           const gchar  *iname,
           Lane        **out_lane,
           guint        *out_pos)
{
	guint mine = lane_index (self);
	guint pass, i;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < self->lanes->len; i++)
		{
			Lane *lane = g_ptr_array_index (self->lanes, i);
			gint  k;

			if ((pass == 0) != (i == mine))
				continue;
			for (k = (gint) lane->stack->len - 1; k >= 0; k--)
				if (g_array_index (lane->stack, OpenEntry, k).name == iname)
				{
					*out_lane = lane;
					*out_pos = (guint) k;
					return TRUE;
				}
		}
	return FALSE;
}

void
lrg_profiler_end_section (LrgProfiler *self,
                          const gchar *name)
{
	const gchar *iname;
	gint64       now;
	Lane        *lane = NULL;
	OpenEntry    o;
	guint        pos = 0;

	g_return_if_fail (LRG_IS_PROFILER (self));
	g_return_if_fail (name != NULL);

	if (G_LIKELY (!self->enabled))
		return;

	now = g_get_monotonic_time ();
	iname = g_intern_string (name);
	g_mutex_lock (&self->lock);
	if (!find_open (self, iname, &lane, &pos))
	{
		g_mutex_unlock (&self->lock);
		lrg_warning (LRG_LOG_DOMAIN_DEBUG, "end_section called without matching begin_section: %s", name);
		return;
	}
	o = g_array_index (lane->stack, OpenEntry, pos);
	g_array_remove_index (lane->stack, pos);

	/* Close the frame event (if it is in the frame being recorded) */
	if (self->cur != NULL && o.event >= 0 && o.frame == self->cur->index)
	{
		LrgProfilerEvent *e = &g_array_index (self->cur->events, LrgProfilerEvent, o.event);

		e->duration_us = now - e->start_us;
	}

	/* Statistics use the whole duration, across frames */
	stats_add (stats_for (self, iname), now - o.start_us);
	g_mutex_unlock (&self->lock);
}

gboolean
lrg_profiler_is_section_active (LrgProfiler *self,
                                const gchar *name)
{
	const gchar *iname;
	Lane        *lane;
	guint        pos;
	gboolean     found = FALSE;
	guint        i;

	g_return_val_if_fail (LRG_IS_PROFILER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	iname = g_intern_string (name);
	g_mutex_lock (&self->lock);
	for (i = 0; i < self->lanes->len && !found; i++)
	{
		lane = g_ptr_array_index (self->lanes, i);
		for (pos = 0; pos < lane->stack->len; pos++)
			if (g_array_index (lane->stack, OpenEntry, pos).name == iname)
			{
				found = TRUE;
				break;
			}
	}
	g_mutex_unlock (&self->lock);
	return found;
}

GList *
lrg_profiler_get_active_section_names (LrgProfiler *self)
{
	GList *out = NULL;
	guint  i, k;

	g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);

	g_mutex_lock (&self->lock);
	for (i = 0; i < self->lanes->len; i++)
	{
		Lane *lane = g_ptr_array_index (self->lanes, i);

		for (k = 0; k < lane->stack->len; k++)
		{
			const gchar *name = g_array_index (lane->stack, OpenEntry, k).name;

			if (g_list_find (out, name) == NULL)
				out = g_list_prepend (out, (gpointer) name);
		}
	}
	g_mutex_unlock (&self->lock);
	return g_list_reverse (out);
}

/* --------------------------------------------------------------------------
 * Frames
 * -------------------------------------------------------------------------- */

/* GPU timer results go into their frame. */
static void
on_gpu_result (guint64      frame_index,
               const gchar *name,
               guint        depth,
               gint64       start_us,
               gint64       duration_us,
               gpointer     user_data)
{
	lrg_profiler_add_gpu_event (LRG_PROFILER (user_data), frame_index, name, depth, start_us, duration_us);
}

void
lrg_profiler_begin_frame (LrgProfiler *self)
{
	gint64 now;

	g_return_if_fail (LRG_IS_PROFILER (self));

	if (G_LIKELY (!self->enabled))
		return;

	/* GPU results of earlier frames first (add_gpu_event locks) */
	if (self->gpu_enabled)
	{
		if (self->gpu_timer == NULL)
			self->gpu_timer = lrg_gpu_timer_new ();
		lrg_gpu_timer_collect (self->gpu_timer, on_gpu_result, self);
	}

	now = g_get_monotonic_time ();
	g_mutex_lock (&self->lock);
	/* Sections recorded since the last end_frame stay in this frame */
	if (self->cur == NULL)
		ensure_frame (self, now);
	else if (self->cur->events->len == 0 && self->cur->counters->len == 0)
		self->cur->start_us = now;
	g_mutex_unlock (&self->lock);
	self->frame_start = now;

	if (self->gpu_enabled)
		lrg_gpu_timer_begin_frame (self->gpu_timer, self->frame_counter);
}

void
lrg_profiler_end_frame (LrgProfiler *self)
{
	gint64 now;
	gint64 elapsed;
	Frame *f;
	guint  i, k;

	g_return_if_fail (LRG_IS_PROFILER (self));

	if (G_LIKELY (!self->enabled))
		return;

	if (self->gpu_enabled && self->gpu_timer != NULL)
		lrg_gpu_timer_end_frame (self->gpu_timer);

	now = g_get_monotonic_time ();
	g_mutex_lock (&self->lock);
	f = ensure_frame (self, now);
	f->end_us = now;

	/* Sections still open are cut here and carried into the next frame */
	for (i = 0; i < self->lanes->len; i++)
	{
		Lane *lane = g_ptr_array_index (self->lanes, i);

		for (k = 0; k < lane->stack->len; k++)
		{
			OpenEntry *o = &g_array_index (lane->stack, OpenEntry, k);

			if (o->frame == f->index && o->event >= 0)
			{
				LrgProfilerEvent *e = &g_array_index (f->events, LrgProfilerEvent, o->event);

				e->duration_us = now - e->start_us;
			}
			o->frame = G_MAXUINT64;
			o->event = -1;
		}
	}
	/* Events of sections ended by another thread after a cut, or never
	 * closed, end with the frame */
	for (i = 0; i < f->events->len; i++)
	{
		LrgProfilerEvent *e = &g_array_index (f->events, LrgProfilerEvent, i);

		if (e->duration_us < 0)
			e->duration_us = now - e->start_us;
	}

	g_queue_push_tail (&self->ring, f);
	while (g_queue_get_length (&self->ring) > self->capacity)
		frame_free (g_queue_pop_head (&self->ring));
	self->cur = NULL;
	self->frame_counter++;
	self->overflow_warned = FALSE;
	g_mutex_unlock (&self->lock);

	/* 0.8 frame time and fps */
	if (self->frame_start > 0)
		self->last_frame_time_us = now - self->frame_start;
	self->frame_count++;
	elapsed = now - self->fps_timer_start;
	if (elapsed >= 1000000)
	{
		self->fps = (gdouble) self->frame_count / ((gdouble) elapsed / 1000000.0);
		self->frame_count = 0;
		self->fps_timer_start = now;
	}
}

guint64
lrg_profiler_get_frame_index (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
	return self->frame_counter;
}

guint
lrg_profiler_get_n_frames (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
	return g_queue_get_length (&self->ring);
}

void
lrg_profiler_set_counter (LrgProfiler *self,
                          const gchar *name,
                          gdouble      value)
{
	const gchar *iname;

	g_return_if_fail (LRG_IS_PROFILER (self));
	g_return_if_fail (name != NULL);

	if (G_LIKELY (!self->enabled))
		return;
	iname = g_intern_string (name);
	g_mutex_lock (&self->lock);
	frame_set_counter (ensure_frame (self, g_get_monotonic_time ()), iname, value);
	g_mutex_unlock (&self->lock);
}

/* Copies @f into a capture frame with times relative to @base. */
static Frame *
frame_copy_relative (const Frame *f,
                     gint64       base)
{
	Frame *c = frame_new (f->index);
	guint  i;

	c->start_us = f->start_us - base;
	c->duration_us = MAX (f->end_us - f->start_us, 0);
	g_array_append_vals (c->events, f->events->data, f->events->len);
	g_array_append_vals (c->gpu, f->gpu->data, f->gpu->len);
	g_array_append_vals (c->counters, f->counters->data, f->counters->len);
	for (i = 0; i < c->events->len; i++)
		g_array_index (c->events, LrgProfilerEvent, i).start_us -= f->start_us;
	for (i = 0; i < c->gpu->len; i++)
	{
		LrgProfilerEvent *e = &g_array_index (c->gpu, LrgProfilerEvent, i);

		e->start_us -= f->start_us;
		if (e->depth == 0)
			c->gpu_us = MAX (c->gpu_us, 0) + e->duration_us;
	}
	return c;
}

LrgProfilerCapture *
lrg_profiler_capture (LrgProfiler *self,
                      guint        n_frames)
{
	LrgProfilerCapture *cap;
	GList              *l;
	guint               total, skip, i;
	gint64              base = 0;
	gboolean            first = TRUE;

	g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);

	cap = lrg_profiler_capture_new ();
	g_mutex_lock (&self->lock);
	for (i = 0; i < self->lanes->len; i++)
		g_ptr_array_add (cap->threads, g_strdup (((Lane *) g_ptr_array_index (self->lanes, i))->name));
	total = g_queue_get_length (&self->ring);
	skip = (n_frames == 0 || n_frames >= total) ? 0 : total - n_frames;
	for (l = self->ring.head, i = 0; l != NULL; l = l->next, i++)
	{
		Frame *f = l->data;

		if (i < skip)
			continue;
		if (first)
		{
			base = f->start_us;
			first = FALSE;
		}
		g_ptr_array_add (cap->frames, frame_copy_relative (f, base));
	}
	g_mutex_unlock (&self->lock);
	return cap;
}

/* --------------------------------------------------------------------------
 * GPU lane
 * -------------------------------------------------------------------------- */

gboolean
lrg_profiler_get_gpu_enabled (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), FALSE);
	return self->gpu_enabled;
}

void
lrg_profiler_set_gpu_enabled (LrgProfiler *self,
                              gboolean     enabled)
{
	g_return_if_fail (LRG_IS_PROFILER (self));

	enabled = !!enabled;
	if (self->gpu_enabled == enabled)
		return;
	self->gpu_enabled = enabled;
	/* The timer exists as soon as the lane is on, so is_supported()
	 * can be asked before the next begin_frame().  Created idle: no
	 * GL until begin_frame / is_supported, and never a glFinish. */
	if (enabled && self->gpu_timer == NULL)
		self->gpu_timer = lrg_gpu_timer_new ();
	if (!enabled && self->gpu_timer != NULL)
		lrg_gpu_timer_end_frame (self->gpu_timer);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GPU_ENABLED]);
}

LrgGpuTimer *
lrg_profiler_get_gpu_timer (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);
	return self->gpu_timer;
}

void
lrg_profiler_begin_gpu_section (LrgProfiler *self,
                                const gchar *name)
{
	g_return_if_fail (LRG_IS_PROFILER (self));

	if (G_LIKELY (!self->enabled || !self->gpu_enabled || self->gpu_timer == NULL))
		return;
	lrg_gpu_timer_begin (self->gpu_timer, name);
}

void
lrg_profiler_end_gpu_section (LrgProfiler *self)
{
	g_return_if_fail (LRG_IS_PROFILER (self));

	if (G_LIKELY (!self->enabled || !self->gpu_enabled || self->gpu_timer == NULL))
		return;
	lrg_gpu_timer_end (self->gpu_timer);
}

gboolean
lrg_profiler_add_gpu_event (LrgProfiler *self,
                            guint64      frame_index,
                            const gchar *name,
                            guint        depth,
                            gint64       start_us,
                            gint64       duration_us)
{
	Frame *f = NULL;
	GList *l;

	g_return_val_if_fail (LRG_IS_PROFILER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	g_mutex_lock (&self->lock);
	if (self->cur != NULL && self->cur->index == frame_index)
		f = self->cur;
	for (l = self->ring.tail; f == NULL && l != NULL; l = l->prev)
		if (((Frame *) l->data)->index == frame_index)
			f = l->data;
	if (f != NULL)
		frame_add_gpu (f, g_intern_string (name), depth, start_us, MAX (duration_us, 0));
	g_mutex_unlock (&self->lock);
	return f != NULL;
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

GList *
lrg_profiler_get_section_names (LrgProfiler *self)
{
	GList *keys;

	g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);
	g_mutex_lock (&self->lock);
	keys = g_hash_table_get_keys (self->sections);
	g_mutex_unlock (&self->lock);
	return keys;
}

/* Statistics of @name or NULL (lock held by the caller). */
static SectionStats *
stats_lookup (LrgProfiler *self,
              const gchar *name)
{
	return g_hash_table_lookup (self->sections, name);
}

LrgProfilerSample *
lrg_profiler_get_last_sample (LrgProfiler *self,
                              const gchar *name)
{
	SectionStats      *s;
	LrgProfilerSample *out = NULL;

	g_return_val_if_fail (LRG_IS_PROFILER (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	g_mutex_lock (&self->lock);
	s = stats_lookup (self, name);
	if (s != NULL && s->count > 0)
		out = sample_new (name, s->last_us);
	g_mutex_unlock (&self->lock);
	return out;
}

gdouble
lrg_profiler_get_average_ms (LrgProfiler *self,
                             const gchar *name)
{
	SectionStats *s;
	gdouble       out = 0.0;

	g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
	g_return_val_if_fail (name != NULL, 0.0);

	g_mutex_lock (&self->lock);
	s = stats_lookup (self, name);
	if (s != NULL && s->count > 0)
		out = ((gdouble) s->total_us / (gdouble) s->count) / 1000.0;
	g_mutex_unlock (&self->lock);
	return out;
}

gdouble
lrg_profiler_get_min_ms (LrgProfiler *self,
                         const gchar *name)
{
	SectionStats *s;
	gdouble       out = 0.0;

	g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
	g_return_val_if_fail (name != NULL, 0.0);

	g_mutex_lock (&self->lock);
	s = stats_lookup (self, name);
	if (s != NULL && s->min_us != G_MAXINT64)
		out = (gdouble) s->min_us / 1000.0;
	g_mutex_unlock (&self->lock);
	return out;
}

gdouble
lrg_profiler_get_max_ms (LrgProfiler *self,
                         const gchar *name)
{
	SectionStats *s;
	gdouble       out = 0.0;

	g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
	g_return_val_if_fail (name != NULL, 0.0);

	g_mutex_lock (&self->lock);
	s = stats_lookup (self, name);
	if (s != NULL)
		out = (gdouble) s->max_us / 1000.0;
	g_mutex_unlock (&self->lock);
	return out;
}

guint
lrg_profiler_get_sample_count (LrgProfiler *self,
                               const gchar *name)
{
	SectionStats *s;
	guint         out = 0;

	g_return_val_if_fail (LRG_IS_PROFILER (self), 0);
	g_return_val_if_fail (name != NULL, 0);

	g_mutex_lock (&self->lock);
	s = stats_lookup (self, name);
	if (s != NULL)
		out = s->count;
	g_mutex_unlock (&self->lock);
	return out;
}

gdouble
lrg_profiler_get_frame_time_ms (LrgProfiler *self)
{
	g_return_val_if_fail (LRG_IS_PROFILER (self), 0.0);
	return (gdouble) self->last_frame_time_us / 1000.0;
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
	guint i;

	g_return_if_fail (LRG_IS_PROFILER (self));

	g_mutex_lock (&self->lock);
	g_hash_table_remove_all (self->sections);
	for (i = 0; i < self->lanes->len; i++)
		g_array_set_size (((Lane *) g_ptr_array_index (self->lanes, i))->stack, 0);
	g_clear_pointer (&self->cur, frame_free);
	g_queue_clear_full (&self->ring, frame_free);
	g_queue_init (&self->ring);
	g_mutex_unlock (&self->lock);

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
	const gchar *iname;
	guint        i;

	g_return_if_fail (LRG_IS_PROFILER (self));
	g_return_if_fail (name != NULL);

	iname = g_intern_string (name);
	g_mutex_lock (&self->lock);
	g_hash_table_remove (self->sections, iname);
	for (i = 0; i < self->lanes->len; i++)
	{
		Lane *lane = g_ptr_array_index (self->lanes, i);
		gint  k;

		for (k = (gint) lane->stack->len - 1; k >= 0; k--)
			if (g_array_index (lane->stack, OpenEntry, k).name == iname)
				g_array_remove_index (lane->stack, (guint) k);
	}
	g_mutex_unlock (&self->lock);
}
