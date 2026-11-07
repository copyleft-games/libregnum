/* lrg-gpu-timer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuTimer: GL timestamp queries in a ring of frame slots.
 *
 * Each slot holds the frame number, the CPU/GPU clock calibration taken
 * at the frame start and an array of sections, each with two query
 * objects (begin and end timestamps).  Query objects are pooled per slot
 * and reused, so a steady frame allocates nothing.  The GL entry points
 * are resolved with rlGetProcAddress(), the way lrg-gpu-particles.c and
 * lrg-light-set.c do: graylib does not wrap timer queries.
 */

#include "config.h"

#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-gpu-timer.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * GL entry points
 * ========================================================================== */

#define GL_TIMESTAMP_                (0x8E28)
#define GL_QUERY_RESULT_             (0x8866)
#define GL_QUERY_RESULT_AVAILABLE_   (0x8867)

typedef void (*GlGenQueries) (gint n, guint *ids);
typedef void (*GlDeleteQueries) (gint n, const guint *ids);
typedef void (*GlQueryCounter) (guint id, guint target);
typedef void (*GlGetQueryObjectiv) (guint id, guint pname, gint *params);
typedef void (*GlGetQueryObjectui64v) (guint id, guint pname, guint64 *params);
typedef void (*GlGetInteger64v) (guint pname, gint64 *data);

typedef struct
{
	gboolean              tried;
	gboolean              ok;
	GlGenQueries          gen_queries;
	GlDeleteQueries       delete_queries;
	GlQueryCounter        query_counter;
	GlGetQueryObjectiv    get_query_objectiv;
	GlGetQueryObjectui64v get_query_objectui64v;
	GlGetInteger64v       get_integer64v;
} TimerGl;

static TimerGl gl;

#define LOAD(field, name) gl.field = (__typeof__ (gl.field)) rlGetProcAddress (name)

/*
 * Resolves the timer-query entry points once.  rlGetProcAddress() only
 * works with a window (a GL context) open; before that the timer stays
 * unsupported and tries again on the next call.
 */
static gboolean
gl_load (void)
{
	if (gl.tried)
		return gl.ok;
	if (!IsWindowReady ())
		return FALSE;
	gl.tried = TRUE;

	LOAD (gen_queries, "glGenQueries");
	LOAD (delete_queries, "glDeleteQueries");
	LOAD (query_counter, "glQueryCounter");
	LOAD (get_query_objectiv, "glGetQueryObjectiv");
	LOAD (get_query_objectui64v, "glGetQueryObjectui64v");
	LOAD (get_integer64v, "glGetInteger64v");
	gl.ok = gl.gen_queries != NULL && gl.delete_queries != NULL && gl.query_counter != NULL &&
	        gl.get_query_objectiv != NULL && gl.get_query_objectui64v != NULL && gl.get_integer64v != NULL;
	if (!gl.ok)
		lrg_info (LRG_LOG_DOMAIN_DEBUG, "GPU timer: no timestamp queries (GL_ARB_timer_query missing)");
	return gl.ok;
}

/* ==========================================================================
 * Slots
 * ========================================================================== */

typedef struct
{
	const gchar *name;     /* interned */
	guint        depth;
	guint        q_begin;  /* query ids from the slot's pool */
	guint        q_end;
	gboolean     closed;
} Section;

typedef struct
{
	guint64  frame_index;
	gboolean in_use;       /* has sections waiting for results */
	gint64   cpu_us;       /* monotonic time at the frame start */
	gint64   gpu_ns;       /* GL_TIMESTAMP at the frame start */
	GArray  *sections;     /* Section */
	GArray  *pool;         /* guint query ids, 2 per section, reused */
} Slot;

struct _LrgGpuTimer
{
	GObject  parent_instance;

	Slot     slots[LRG_GPU_TIMER_FRAMES_IN_FLIGHT];
	guint    current;        /* slot being recorded */
	gboolean in_frame;
	guint    depth;          /* open sections */
	GArray  *open;           /* guint section indices in the current slot */
	guint    dropped;
	gint64   last_frame_us;
};

G_DEFINE_FINAL_TYPE (LrgGpuTimer, lrg_gpu_timer, G_TYPE_OBJECT)

/* Deletes every pooled query of every slot (GL must be current). */
static void
release_queries (LrgGpuTimer *self)
{
	guint i;

	for (i = 0; i < LRG_GPU_TIMER_FRAMES_IN_FLIGHT; i++)
	{
		Slot *s = &self->slots[i];

		if (s->pool->len > 0 && gl.ok && IsWindowReady ())
			gl.delete_queries ((gint) s->pool->len, (const guint *) s->pool->data);
		g_array_set_size (s->pool, 0);
		g_array_set_size (s->sections, 0);
		s->in_use = FALSE;
	}
	g_array_set_size (self->open, 0);
	self->depth = 0;
	self->in_frame = FALSE;
}

static void
lrg_gpu_timer_finalize (GObject *object)
{
	LrgGpuTimer *self = LRG_GPU_TIMER (object);
	guint        i;

	release_queries (self);
	for (i = 0; i < LRG_GPU_TIMER_FRAMES_IN_FLIGHT; i++)
	{
		g_array_unref (self->slots[i].sections);
		g_array_unref (self->slots[i].pool);
	}
	g_array_unref (self->open);
	G_OBJECT_CLASS (lrg_gpu_timer_parent_class)->finalize (object);
}

static void
lrg_gpu_timer_class_init (LrgGpuTimerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gpu_timer_finalize;
}

static void
lrg_gpu_timer_init (LrgGpuTimer *self)
{
	guint i;

	for (i = 0; i < LRG_GPU_TIMER_FRAMES_IN_FLIGHT; i++)
	{
		self->slots[i].sections = g_array_new (FALSE, TRUE, sizeof (Section));
		self->slots[i].pool = g_array_new (FALSE, TRUE, sizeof (guint));
	}
	self->open = g_array_new (FALSE, FALSE, sizeof (guint));
	self->last_frame_us = -1;
}

LrgGpuTimer *
lrg_gpu_timer_new (void)
{
	return g_object_new (LRG_TYPE_GPU_TIMER, NULL);
}

gboolean
lrg_gpu_timer_is_supported (LrgGpuTimer *self)
{
	g_return_val_if_fail (LRG_IS_GPU_TIMER (self), FALSE);
	return gl_load ();
}

/*
 * Returns query id @n of the current slot's pool, growing the pool (in
 * chunks of 32 queries) when the frame times more sections than before.
 */
static guint
pool_query (Slot  *s,
            guint  n)
{
	if (n >= s->pool->len)
	{
		guint old = s->pool->len;
		guint grow = MAX (32u, n + 1 - old);

		g_array_set_size (s->pool, old + grow);
		gl.gen_queries ((gint) grow, &g_array_index (s->pool, guint, old));
	}
	return g_array_index (s->pool, guint, n);
}

void
lrg_gpu_timer_begin_frame (LrgGpuTimer *self,
                           guint64      frame_index)
{
	Slot *s;

	g_return_if_fail (LRG_IS_GPU_TIMER (self));

	if (!gl_load ())
		return;
	if (self->in_frame)
		lrg_gpu_timer_end_frame (self);

	/* The next slot; results it still waits for are given up (never
	 * block on the GPU) */
	self->current = (self->current + 1) % LRG_GPU_TIMER_FRAMES_IN_FLIGHT;
	s = &self->slots[self->current];
	if (s->in_use)
		self->dropped += s->sections->len;
	g_array_set_size (s->sections, 0);
	s->in_use = FALSE;
	s->frame_index = frame_index;

	/* Clock calibration: the GPU's "now" next to the CPU's */
	s->gpu_ns = 0;
	gl.get_integer64v (GL_TIMESTAMP_, &s->gpu_ns);
	s->cpu_us = g_get_monotonic_time ();

	g_array_set_size (self->open, 0);
	self->depth = 0;
	self->in_frame = TRUE;
}

void
lrg_gpu_timer_end_frame (LrgGpuTimer *self)
{
	g_return_if_fail (LRG_IS_GPU_TIMER (self));

	if (!self->in_frame)
		return;
	while (self->open->len > 0)
		lrg_gpu_timer_end (self);
	self->slots[self->current].in_use = self->slots[self->current].sections->len > 0;
	self->in_frame = FALSE;
}

void
lrg_gpu_timer_begin (LrgGpuTimer *self,
                     const gchar *name)
{
	Slot    *s;
	Section  sec;
	guint    idx;

	g_return_if_fail (LRG_IS_GPU_TIMER (self));
	g_return_if_fail (name != NULL);

	if (!self->in_frame)
		return;
	s = &self->slots[self->current];
	if (s->sections->len >= LRG_GPU_TIMER_MAX_SECTIONS)
	{
		self->dropped++;
		/* still push a marker so the matching end pops nothing real */
		idx = G_MAXUINT;
		g_array_append_val (self->open, idx);
		return;
	}

	/* Work batched so far belongs before the section */
	rlDrawRenderBatchActive ();

	memset (&sec, 0, sizeof sec);
	sec.name = g_intern_string (name);
	sec.depth = self->open->len;
	sec.q_begin = pool_query (s, s->sections->len * 2);
	sec.q_end = pool_query (s, s->sections->len * 2 + 1);
	gl.query_counter (sec.q_begin, GL_TIMESTAMP_);
	idx = s->sections->len;
	g_array_append_val (s->sections, sec);
	g_array_append_val (self->open, idx);
}

void
lrg_gpu_timer_end (LrgGpuTimer *self)
{
	Slot  *s;
	guint  idx;

	g_return_if_fail (LRG_IS_GPU_TIMER (self));

	if (!self->in_frame || self->open->len == 0)
		return;
	idx = g_array_index (self->open, guint, self->open->len - 1);
	g_array_set_size (self->open, self->open->len - 1);
	if (idx == G_MAXUINT)
		return;
	s = &self->slots[self->current];
	rlDrawRenderBatchActive ();
	gl.query_counter (g_array_index (s->sections, Section, idx).q_end, GL_TIMESTAMP_);
	g_array_index (s->sections, Section, idx).closed = TRUE;
}

/*
 * Whether every query of @s has a result: the last end query finishing
 * implies the earlier ones did (timestamps complete in order).
 */
static gboolean
slot_available (Slot *s)
{
	Section *last;
	gint     avail = 0;

	if (s->sections->len == 0)
		return TRUE;
	last = &g_array_index (s->sections, Section, s->sections->len - 1);
	gl.get_query_objectiv (last->q_end, GL_QUERY_RESULT_AVAILABLE_, &avail);
	if (avail == 0)
		return FALSE;
	gl.get_query_objectiv (g_array_index (s->sections, Section, 0).q_end, GL_QUERY_RESULT_AVAILABLE_, &avail);
	return avail != 0;
}

guint
lrg_gpu_timer_collect (LrgGpuTimer           *self,
                       LrgGpuTimerResultFunc  func,
                       gpointer               user_data)
{
	guint collected = 0;
	guint k;

	g_return_val_if_fail (LRG_IS_GPU_TIMER (self), 0);

	if (!gl.ok)
		return 0;

	/* Oldest slots first: the one after the current one is the oldest */
	for (k = 1; k <= LRG_GPU_TIMER_FRAMES_IN_FLIGHT; k++)
	{
		Slot   *s = &self->slots[(self->current + k) % LRG_GPU_TIMER_FRAMES_IN_FLIGHT];
		gint64  top = 0;
		guint   i;

		if (!s->in_use || (s == &self->slots[self->current] && self->in_frame))
			continue;
		if (!slot_available (s))
			continue;
		for (i = 0; i < s->sections->len; i++)
		{
			Section *sec = &g_array_index (s->sections, Section, i);
			guint64  t0 = 0, t1 = 0;
			gint64   start_us, dur_us;

			if (!sec->closed)
				continue;
			gl.get_query_objectui64v (sec->q_begin, GL_QUERY_RESULT_, &t0);
			gl.get_query_objectui64v (sec->q_end, GL_QUERY_RESULT_, &t1);
			/* GPU nanoseconds onto the CPU's monotonic microseconds */
			start_us = s->cpu_us + ((gint64) t0 - s->gpu_ns) / 1000;
			dur_us = (t1 > t0) ? (gint64) ((t1 - t0) / 1000u) : 0;
			if (sec->depth == 0)
				top += dur_us;
			if (func != NULL)
				func (s->frame_index, sec->name, sec->depth, start_us, dur_us, user_data);
		}
		self->last_frame_us = top;
		s->in_use = FALSE;
		g_array_set_size (s->sections, 0);
		collected++;
	}
	return collected;
}

gint64
lrg_gpu_timer_get_last_frame_us (LrgGpuTimer *self)
{
	g_return_val_if_fail (LRG_IS_GPU_TIMER (self), -1);
	return self->last_frame_us;
}

guint
lrg_gpu_timer_get_dropped (LrgGpuTimer *self)
{
	g_return_val_if_fail (LRG_IS_GPU_TIMER (self), 0);
	return self->dropped;
}

void
lrg_gpu_timer_release (LrgGpuTimer *self)
{
	g_return_if_fail (LRG_IS_GPU_TIMER (self));
	release_queries (self);
}
