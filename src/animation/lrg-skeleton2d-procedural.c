/* lrg-skeleton2d-procedural.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DProcedural: blink and breathe layers over the slot
 * blend-shape weights of an LrgSkeleton2D.  Blinks are deterministic: a
 * small seeded generator (splitmix32) draws the intervals, and the blink
 * start times are cached as they are first needed, so any time can be
 * evaluated (forwards, backwards, scrubbed) with the same result.
 */

#include "config.h"

#include <math.h>

#include "animation/lrg-skeleton2d-procedural.h"
#include "animation/lrg-shape-sheet.h"

typedef enum
{
	LAYER_BLINK,
	LAYER_BREATHE
} LayerKind;

typedef struct
{
	LayerKind  kind;
	gchar     *slot;

	/* blink */
	gdouble    min_interval;
	gdouble    max_interval;
	gdouble    duration;
	guint32    rng;       /* generator state after the last cached start */
	GArray    *starts;    /* gdouble, blink start times, increasing */

	/* breathe */
	gdouble    period;
	gfloat     amount;
} Layer;

struct _LrgSkeleton2DProcedural
{
	GObject    parent_instance;

	GPtrArray *layers;   /* Layer* */
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2DProcedural, lrg_skeleton2d_procedural, G_TYPE_OBJECT)

static void
layer_free (gpointer data)
{
	Layer *l = data;

	g_clear_pointer (&l->starts, g_array_unref);
	g_free (l->slot);
	g_free (l);
}

static void
lrg_skeleton2d_procedural_finalize (GObject *object)
{
	LrgSkeleton2DProcedural *self = LRG_SKELETON2D_PROCEDURAL (object);

	g_clear_pointer (&self->layers, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_skeleton2d_procedural_parent_class)->finalize (object);
}

static void
lrg_skeleton2d_procedural_class_init (LrgSkeleton2DProceduralClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_skeleton2d_procedural_finalize;
}

static void
lrg_skeleton2d_procedural_init (LrgSkeleton2DProcedural *self)
{
	self->layers = g_ptr_array_new_with_free_func (layer_free);
}

LrgSkeleton2DProcedural *
lrg_skeleton2d_procedural_new (void)
{
	return g_object_new (LRG_TYPE_SKELETON2D_PROCEDURAL, NULL);
}

/* ------------------------------------------------------------------ */
/* Layers                                                              */
/* ------------------------------------------------------------------ */

gboolean
lrg_skeleton2d_procedural_add_blink (LrgSkeleton2DProcedural *self,
                                     const gchar             *slot,
                                     gfloat                   min_interval,
                                     gfloat                   max_interval,
                                     gfloat                   duration,
                                     guint32                  seed)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self), FALSE);

	if (slot == NULL || slot[0] == '\0')
		return FALSE;
	if (!isfinite (min_interval) || !isfinite (max_interval) || !isfinite (duration))
		return FALSE;
	if (!(min_interval > 0.0f) || max_interval < min_interval || !(duration > 0.0f))
		return FALSE;

	l = g_new0 (Layer, 1);
	l->kind = LAYER_BLINK;
	l->slot = g_strdup (slot);
	l->min_interval = min_interval;
	l->max_interval = max_interval;
	/* one blink at a time: never longer than the shortest interval */
	l->duration = MIN (duration, min_interval);
	l->rng = seed;
	l->starts = g_array_new (FALSE, FALSE, sizeof (gdouble));
	g_ptr_array_add (self->layers, l);
	return TRUE;
}

gboolean
lrg_skeleton2d_procedural_add_breathe (LrgSkeleton2DProcedural *self,
                                       const gchar             *slot,
                                       gfloat                   period,
                                       gfloat                   amount)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self), FALSE);

	if (slot == NULL || slot[0] == '\0' || !isfinite (period) || !(period > 0.0f))
		return FALSE;
	if (!isfinite (amount) || amount < 0.0f || amount > 1.0f)
		return FALSE;

	l = g_new0 (Layer, 1);
	l->kind = LAYER_BREATHE;
	l->slot = g_strdup (slot);
	l->period = period;
	l->amount = amount;
	g_ptr_array_add (self->layers, l);
	return TRUE;
}

guint
lrg_skeleton2d_procedural_get_layer_count (LrgSkeleton2DProcedural *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self), 0);

	return self->layers->len;
}

void
lrg_skeleton2d_procedural_clear (LrgSkeleton2DProcedural *self)
{
	g_return_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self));

	g_ptr_array_set_size (self->layers, 0);
}

/* ------------------------------------------------------------------ */
/* Evaluation                                                          */
/* ------------------------------------------------------------------ */

/* splitmix32: a tiny, well-mixed, platform-independent generator */
static guint32
next_u32 (guint32 *state)
{
	guint32 z;

	*state += 0x9E3779B9u;
	z = *state;
	z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
	z = (z ^ (z >> 13)) * 0xC2B2AE35u;
	return z ^ (z >> 16);
}

/* uniform in [0, 1) with 24 bits */
static gdouble
next_unit (guint32 *state)
{
	return (gdouble)(next_u32 (state) >> 8) / 16777216.0;
}

static gdouble
blink_weight (Layer   *l,
              gdouble  time)
{
	gdouble *starts;
	guint    lo;
	guint    hi;
	gdouble  f;

	if (!(time >= 0.0) || !isfinite (time))
		return 0.0;

	/* cache start times until one lies beyond @time */
	while (l->starts->len == 0 || g_array_index (l->starts, gdouble, l->starts->len - 1) <= time)
	{
		gdouble prev = l->starts->len > 0 ? g_array_index (l->starts, gdouble, l->starts->len - 1) : 0.0;
		gdouble next = prev + l->min_interval + (l->max_interval - l->min_interval) * next_unit (&l->rng);

		g_array_append_val (l->starts, next);
	}

	/* the last start at or before @time (binary search) */
	starts = (gdouble *)(gpointer)l->starts->data;
	if (time < starts[0])
		return 0.0;
	lo = 0;
	hi = l->starts->len - 1;   /* starts[hi] > time */
	while (hi - lo > 1)
	{
		guint mid = (lo + hi) / 2;

		if (starts[mid] <= time)
			lo = mid;
		else
			hi = mid;
	}

	/* a triangle: closed at the middle of the blink */
	f = (time - starts[lo]) / l->duration;
	if (f >= 1.0)
		return 0.0;
	return 1.0 - fabs (2.0 * f - 1.0);
}

gfloat
lrg_skeleton2d_procedural_get_blink_weight (LrgSkeleton2DProcedural *self,
                                            guint                    index,
                                            gdouble                  time)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self), 0.0f);

	if (index >= self->layers->len)
		return 0.0f;
	l = g_ptr_array_index (self->layers, index);
	if (l->kind != LAYER_BLINK)
		return 0.0f;
	return (gfloat)blink_weight (l, time);
}

void
lrg_skeleton2d_procedural_apply (LrgSkeleton2DProcedural *self,
                                 LrgSkeleton2D           *skeleton,
                                 gdouble                  time)
{
	guint i;

	g_return_if_fail (LRG_IS_SKELETON2D_PROCEDURAL (self));
	g_return_if_fail (LRG_IS_SKELETON2D (skeleton));

	for (i = 0; i < self->layers->len; i++)
	{
		Layer *l = g_ptr_array_index (self->layers, i);
		gint   slot = lrg_skeleton2d_find_slot (skeleton, l->slot);

		if (slot < 0)
			continue;
		if (l->kind == LAYER_BLINK)
		{
			/* blink only closes the eyes further than the clip did */
			gfloat w = (gfloat)blink_weight (l, time);
			gfloat cl = lrg_skeleton2d_get_shape_weight (skeleton, slot, LRG_SHAPE_BLINK_L);
			gfloat cr = lrg_skeleton2d_get_shape_weight (skeleton, slot, LRG_SHAPE_BLINK_R);

			lrg_skeleton2d_set_shape_weight (skeleton, slot, LRG_SHAPE_BLINK_L, MAX (cl, w));
			lrg_skeleton2d_set_shape_weight (skeleton, slot, LRG_SHAPE_BLINK_R, MAX (cr, w));
		}
		else
		{
			/* breathing adds on top; the skeleton clamps to 0 .. 1 */
			gdouble phase = 2.0 * G_PI * fmod (MAX (time, 0.0), l->period) / l->period;
			gfloat  w = l->amount * (gfloat)(0.5 * (1.0 - cos (phase)));
			gfloat  c = lrg_skeleton2d_get_shape_weight (skeleton, slot, LRG_SHAPE_BREATHE);

			lrg_skeleton2d_set_shape_weight (skeleton, slot, LRG_SHAPE_BREATHE, c + w);
		}
	}
}
