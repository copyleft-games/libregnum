/* lrg-reflection-probe.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbe: a box-projected local reflection cubemap.  The GPU
 * work (capture into six faces, GGX prefilter) is shared with
 * LrgEnvironment through lrg-environment-glsl-private.h.
 */

#include "config.h"

#include <math.h>
#include <raylib.h>

#include "lrg-reflection-probe.h"
#include "lrg-environment.h"
#include "lrg-environment-glsl-private.h"

struct _LrgReflectionProbe
{
	GObject  parent_instance;

	gfloat   position[3];
	gfloat   extents[3];
	gfloat   blend_distance;
	gfloat   intensity;
	gboolean box_projection;
	guint    resolution;
	gfloat   near_plane;
	gfloat   far_plane;
	gfloat   clear_rgb[3];

	guint    texture;
	guint    mips;
	guint    size;
	guint    captures;
};

G_DEFINE_FINAL_TYPE (LrgReflectionProbe, lrg_reflection_probe, G_TYPE_OBJECT)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
changed (LrgReflectionProbe *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
lrg_reflection_probe_finalize (GObject *object)
{
	LrgReflectionProbe *self = LRG_REFLECTION_PROBE (object);

	if (self->texture != 0 && IsWindowReady ())
		lrg_env_gpu_unload (self->texture);
	self->texture = 0;
	G_OBJECT_CLASS (lrg_reflection_probe_parent_class)->finalize (object);
}

static void
lrg_reflection_probe_class_init (LrgReflectionProbeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_reflection_probe_finalize;

	/**
	 * LrgReflectionProbe::changed:
	 * @self: the probe
	 *
	 * Emitted when a setting changed or a capture finished (the owning
	 * #LrgEnvironment re-emits its own #LrgEnvironment::changed).
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_reflection_probe_init (LrgReflectionProbe *self)
{
	self->extents[0] = self->extents[1] = self->extents[2] = 5.0f;
	self->blend_distance = 1.0f;
	self->intensity = 1.0f;
	self->box_projection = TRUE;
	self->resolution = LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION;
	self->near_plane = 0.05f;
	self->far_plane = 1000.0f;
}

LrgReflectionProbe *
lrg_reflection_probe_new (void)
{
	return g_object_new (LRG_TYPE_REFLECTION_PROBE, NULL);
}

void
lrg_reflection_probe_set_position (LrgReflectionProbe *self,
                                   gfloat              x,
                                   gfloat              y,
                                   gfloat              z)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->position[0] = x;
	self->position[1] = y;
	self->position[2] = z;
	changed (self);
}

void
lrg_reflection_probe_get_position (LrgReflectionProbe *self,
                                   gfloat             *out_position)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	g_return_if_fail (out_position != NULL);
	out_position[0] = self->position[0];
	out_position[1] = self->position[1];
	out_position[2] = self->position[2];
}

void
lrg_reflection_probe_set_extents (LrgReflectionProbe *self,
                                  gfloat              x,
                                  gfloat              y,
                                  gfloat              z)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->extents[0] = MAX (x, 1e-3f);
	self->extents[1] = MAX (y, 1e-3f);
	self->extents[2] = MAX (z, 1e-3f);
	changed (self);
}

void
lrg_reflection_probe_get_extents (LrgReflectionProbe *self,
                                  gfloat             *out_extents)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	g_return_if_fail (out_extents != NULL);
	out_extents[0] = self->extents[0];
	out_extents[1] = self->extents[1];
	out_extents[2] = self->extents[2];
}

void
lrg_reflection_probe_set_blend_distance (LrgReflectionProbe *self,
                                         gfloat              distance)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->blend_distance = MAX (distance, 0.0f);
	changed (self);
}

gfloat
lrg_reflection_probe_get_blend_distance (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 0.0f);
	return self->blend_distance;
}

void
lrg_reflection_probe_set_intensity (LrgReflectionProbe *self,
                                    gfloat              intensity)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->intensity = MAX (intensity, 0.0f);
	changed (self);
}

gfloat
lrg_reflection_probe_get_intensity (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 1.0f);
	return self->intensity;
}

void
lrg_reflection_probe_set_box_projection (LrgReflectionProbe *self,
                                         gboolean            enabled)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->box_projection = enabled != FALSE;
	changed (self);
}

gboolean
lrg_reflection_probe_get_box_projection (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), FALSE);
	return self->box_projection;
}

void
lrg_reflection_probe_set_resolution (LrgReflectionProbe *self,
                                     guint               size)
{
	guint p = 16;

	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));

	size = CLAMP (size, 16u, 1024u);
	while (p * 2 <= size)
		p *= 2;
	self->resolution = p;
	changed (self);
}

guint
lrg_reflection_probe_get_resolution (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION);
	return self->resolution;
}

void
lrg_reflection_probe_set_clip (LrgReflectionProbe *self,
                               gfloat              near_plane,
                               gfloat              far_plane)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	self->near_plane = MAX (near_plane, 1e-4f);
	self->far_plane = MAX (far_plane, self->near_plane * 2.0f);
}

void
lrg_reflection_probe_set_clear_color (LrgReflectionProbe *self,
                                      const gfloat       *rgb)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	g_return_if_fail (rgb != NULL);
	self->clear_rgb[0] = rgb[0];
	self->clear_rgb[1] = rgb[1];
	self->clear_rgb[2] = rgb[2];
}

gboolean
lrg_reflection_probe_contains (LrgReflectionProbe *self,
                               const gfloat       *point)
{
	guint i;

	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), FALSE);
	g_return_val_if_fail (point != NULL, FALSE);

	for (i = 0; i < 3; i++)
		if (fabsf (point[i] - self->position[i]) > self->extents[i])
			return FALSE;
	return TRUE;
}

gfloat
lrg_reflection_probe_get_influence (LrgReflectionProbe *self,
                                    const gfloat       *point)
{
	gfloat m = G_MAXFLOAT;
	guint  i;

	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 0.0f);
	g_return_val_if_fail (point != NULL, 0.0f);

	/* the distance to the nearest face, inside the box (the shader's
	 * lrg_env_probe_weight) */
	for (i = 0; i < 3; i++)
		m = MIN (m, self->extents[i] - fabsf (point[i] - self->position[i]));
	if (m < 0.0f)
		return 0.0f;
	if (self->blend_distance <= 0.0f)
		return 1.0f;
	return CLAMP (m / self->blend_distance, 0.0f, 1.0f);
}

void
lrg_reflection_probe_box_project (LrgReflectionProbe *self,
                                  const gfloat       *point,
                                  const gfloat       *direction,
                                  gfloat             *out_direction)
{
	gfloat r[3];
	gfloat len;
	gfloat t = G_MAXFLOAT;
	guint  i;

	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));
	g_return_if_fail (point != NULL && direction != NULL && out_direction != NULL);

	len = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] +
	             direction[2] * direction[2]);
	for (i = 0; i < 3; i++)
		r[i] = len > 0.0f ? direction[i] / len : (i == 2 ? -1.0f : 0.0f);

	if (!self->box_projection || !lrg_reflection_probe_contains (self, point))
	{
		out_direction[0] = r[0];
		out_direction[1] = r[1];
		out_direction[2] = r[2];
		return;
	}

	/* exit distance of the ray from inside the box: the nearest of the
	 * far slab planes */
	for (i = 0; i < 3; i++)
	{
		gfloat rs = fabsf (r[i]) < 1e-6f ? 1e-6f : r[i];
		gfloat t1 = (self->position[i] + self->extents[i] - point[i]) / rs;
		gfloat t2 = (self->position[i] - self->extents[i] - point[i]) / rs;

		t = MIN (t, MAX (t1, t2));
	}
	t = MAX (t, 0.0f);
	for (i = 0; i < 3; i++)
		out_direction[i] = point[i] + r[i] * t - self->position[i];
	len = sqrtf (out_direction[0] * out_direction[0] + out_direction[1] * out_direction[1] +
	             out_direction[2] * out_direction[2]);
	if (len > 1e-6f)
	{
		out_direction[0] /= len;
		out_direction[1] /= len;
		out_direction[2] /= len;
	}
	else
	{
		out_direction[0] = r[0];
		out_direction[1] = r[1];
		out_direction[2] = r[2];
	}
}

typedef struct
{
	LrgReflectionProbe         *probe;
	LrgReflectionProbeDrawFunc  func;
	gpointer                    data;
} CaptureCtx;

static void
capture_trampoline (guint         face,
                    const gfloat *view,
                    const gfloat *projection,
                    gpointer      user_data)
{
	CaptureCtx *ctx = user_data;

	if (ctx->func != NULL)
		ctx->func (ctx->probe, face, view, projection, ctx->data);
}

gboolean
lrg_reflection_probe_capture (LrgReflectionProbe          *self,
                              LrgReflectionProbeDrawFunc   draw_func,
                              gpointer                     user_data,
                              GError                     **error)
{
	CaptureCtx ctx;
	guint      size;
	guint      mips;
	guint      radiance;
	guint      pre;

	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), FALSE);

	ctx.probe = self;
	ctx.func = draw_func;
	ctx.data = user_data;
	size = self->resolution;
	mips = lrg_env_mip_count_for (size);

	radiance = lrg_env_gpu_capture (size, self->position, self->near_plane, self->far_plane,
	                                self->clear_rgb, capture_trampoline, &ctx, error);
	if (radiance == 0)
		return FALSE;
	pre = lrg_env_gpu_prefilter (radiance, size, size, mips, error);
	lrg_env_gpu_unload (radiance);
	if (pre == 0)
		return FALSE;
	lrg_reflection_probe_take_texture_private (self, pre, mips);
	return TRUE;
}

void
lrg_reflection_probe_take_texture_private (LrgReflectionProbe *probe,
                                           guint               texture,
                                           guint               mips)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (probe));

	if (probe->texture != 0 && probe->texture != texture)
		lrg_env_gpu_unload (probe->texture);
	probe->texture = texture;
	probe->mips = mips;
	probe->size = mips > 0 ? 1u << (mips - 1) : 0;
	probe->captures++;
	changed (probe);
}

gboolean
lrg_reflection_probe_is_ready (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), FALSE);
	return self->texture != 0;
}

guint
lrg_reflection_probe_get_texture_id (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 0);
	return self->texture;
}

guint
lrg_reflection_probe_get_mip_count (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 0);
	return self->texture != 0 ? self->mips : 0;
}

guint
lrg_reflection_probe_get_capture_count (LrgReflectionProbe *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), 0);
	return self->captures;
}

gfloat *
lrg_reflection_probe_read_face (LrgReflectionProbe *self,
                                guint               mip,
                                guint               face,
                                guint              *out_size)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (self), NULL);

	if (self->texture == 0 || mip >= self->mips || face >= 6)
		return NULL;
	if (out_size != NULL)
		*out_size = MAX (self->size >> mip, 1u);
	return lrg_env_gpu_read_face (self->texture, self->size, mip, face);
}

void
lrg_reflection_probe_release_gpu (LrgReflectionProbe *self)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (self));

	if (self->texture != 0 && IsWindowReady ())
		lrg_env_gpu_unload (self->texture);
	self->texture = 0;
	self->mips = 0;
	changed (self);
}
