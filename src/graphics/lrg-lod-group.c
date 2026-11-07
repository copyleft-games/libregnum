/* lrg-lod-group.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodGroup: level-of-detail chains, screen-size selection with
 * hysteresis and bias, and the dithered cross-fade state (LrgLodState).
 *
 * A level is an array of GrlModels drawn together (level 0 is normally the
 * source model; generated levels are one single-mesh model per source
 * mesh, carrying a copy of the source material's maps so textures and
 * shaders are shared, not duplicated). Thresholds are "smallest screen
 * size at which the level is still used" and decrease with the level.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <gio/gio.h>

#include "lrg-lod-group.h"

/* raylib materials always allocate this many maps (MAX_MATERIAL_MAPS in
 * raylib's config.h, which is not public). */
#define LOD_MATERIAL_MAPS (12)

/* ==========================================================================
 * LrgLodState
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgLodState, lrg_lod_state, lrg_lod_state_copy, lrg_lod_state_free)

LrgLodState *
lrg_lod_state_new (void)
{
	LrgLodState *state = g_new0 (LrgLodState, 1);

	lrg_lod_state_init (state);
	return state;
}

LrgLodState *
lrg_lod_state_copy (const LrgLodState *state)
{
	g_return_val_if_fail (state != NULL, NULL);
	return (LrgLodState *) g_memdup2 (state, sizeof (LrgLodState));
}

void
lrg_lod_state_free (LrgLodState *state)
{
	g_free (state);
}

void
lrg_lod_state_init (LrgLodState *state)
{
	g_return_if_fail (state != NULL);

	state->level = LRG_LOD_LEVEL_CULLED;
	state->previous_level = LRG_LOD_LEVEL_CULLED;
	state->fade = 1.0f;
	state->screen_size = 0.0f;
	state->initialized = FALSE;
}

gboolean
lrg_lod_state_is_fading (const LrgLodState *state)
{
	g_return_val_if_fail (state != NULL, FALSE);
	return state->initialized && state->fade < 1.0f;
}

gboolean
lrg_lod_state_get_level_fade (const LrgLodState *state,
                              gint               level,
                              gfloat            *out_fade)
{
	gfloat fade = 0.0f;
	gboolean visible = FALSE;

	g_return_val_if_fail (state != NULL, FALSE);

	if (state->initialized && level >= 0)
	{
		if (level == state->level)
		{
			/* Fading in: draw pixels whose dither value is below fade;
			 * nothing at all on the switching frame. */
			if (state->fade < 1.0f)
			{
				fade = state->fade;
				visible = state->fade > 0.0f;
			}
			else
				visible = TRUE;
		}
		else if (state->fade < 1.0f && level == state->previous_level)
		{
			/* Fading out: the complement (dither >= fade). */
			fade = -state->fade;
			visible = TRUE;
		}
	}
	if (out_fade != NULL)
		*out_fade = visible ? fade : 0.0f;
	return visible;
}

/* ==========================================================================
 * Screen size, bias, shader side
 * ========================================================================== */

static gfloat global_bias = 1.0f;

gfloat
lrg_lod_compute_screen_size (gfloat radius,
                             gfloat distance,
                             gfloat fovy)
{
	gdouble t = tan ((gdouble) fovy * G_PI / 360.0);

	if (t <= 1e-6)
		t = 1e-6;
	if (distance < 1e-4f)
		distance = 1e-4f;
	return (gfloat) ((gdouble) radius / ((gdouble) distance * t));
}

gfloat
lrg_lod_screen_size_from_view_projection (const gfloat *view_projection,
                                          const gfloat *center,
                                          gfloat        radius)
{
	const gfloat *m = view_projection;
	gdouble       p11;
	gdouble       w;

	g_return_val_if_fail (view_projection != NULL, 0.0f);
	g_return_val_if_fail (center != NULL, 0.0f);

	/*
	 * Row 1 of P * V is P[1][1] times the (unit) camera up row, so its
	 * xyz length is the projection's vertical scale: cot (fovy / 2) for
	 * a perspective camera, 2 / height for an orthographic one. Row 3
	 * gives the clip w of the centre: the view depth (perspective) or 1
	 * (orthographic). Column-major: element (row r, col c) is m[c * 4 + r].
	 */
	p11 = sqrt ((gdouble) m[1] * m[1] + (gdouble) m[5] * m[5] + (gdouble) m[9] * m[9]);
	w = (gdouble) m[3] * center[0] + (gdouble) m[7] * center[1] + (gdouble) m[11] * center[2] + m[15];
	w = fabs (w);
	if (w < 1e-4)
		w = 1e-4;
	return (gfloat) ((gdouble) radius * p11 / w);
}

gfloat
lrg_lod_get_global_bias (void)
{
	return global_bias;
}

void
lrg_lod_set_global_bias (gfloat bias)
{
	g_return_if_fail (bias > 0.0f);
	global_bias = bias;
}

static const gchar lod_glsl[] =
	"uniform float lodFade;\n"
	"void lrg_lod_fade_discard ()\n"
	"{\n"
	"    float d;\n"
	"    if (lodFade == 0.0) return;\n"
	"    d = fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715))));\n"
	"    if (lodFade > 0.0 ? (d >= lodFade) : (d < -lodFade)) discard;\n"
	"}\n";

const gchar *
lrg_lod_get_glsl (void)
{
	return lod_glsl;
}

void
lrg_lod_bind_fade (guint  shader_id,
                   gfloat fade)
{
	gint loc;

	if (shader_id == 0 || !IsWindowReady ())
		return;
	/* No location cache: program ids are recycled after a shader hot
	 * reload, and a stale location would hit another uniform. */
	loc = rlGetLocationUniform (shader_id, LRG_LOD_FADE_UNIFORM);
	if (loc < 0)
		return;
	rlEnableShader (shader_id);
	rlSetUniform (loc, &fade, RL_SHADER_UNIFORM_FLOAT, 1);
}

/* ==========================================================================
 * LrgLodGroup
 * ========================================================================== */

typedef struct
{
	GPtrArray *models;       /* GrlModel, owned refs */
	gfloat     screen_size;
	gfloat     error;
} Level;

struct _LrgLodGroup
{
	GObject  parent_instance;

	Level    levels[LRG_LOD_MAX_LEVELS];
	guint    n_levels;

	gboolean has_bounds;
	gboolean bounds_explicit;
	gfloat   bounds_min[3];
	gfloat   bounds_max[3];

	gfloat   hysteresis;
	gfloat   bias;
	gint     force_level;
	gfloat   fade_duration;
};

G_DEFINE_FINAL_TYPE (LrgLodGroup, lrg_lod_group, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_N_LEVELS,
	PROP_HYSTERESIS,
	PROP_BIAS,
	PROP_FORCE_LEVEL,
	PROP_FADE_DURATION,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void
emit_changed (LrgLodGroup *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* Grows @mn/@mx by the box of @mesh's CPU vertices transformed by @t
 * (all eight corners, so rotations such as a Z-up import stay right). */
static gboolean
grow_bounds_mesh (const Mesh   *mesh,
                  const Matrix *t,
                  gfloat       *mn,
                  gfloat       *mx,
                  gboolean      have)
{
	gfloat lo[3];
	gfloat hi[3];
	gint   v;
	guint  c;

	if (mesh->vertices == NULL || mesh->vertexCount <= 0)
		return have;
	lo[0] = hi[0] = mesh->vertices[0];
	lo[1] = hi[1] = mesh->vertices[1];
	lo[2] = hi[2] = mesh->vertices[2];
	for (v = 1; v < mesh->vertexCount; v++)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			gfloat f = mesh->vertices[v * 3 + k];

			if (f < lo[k])
				lo[k] = f;
			if (f > hi[k])
				hi[k] = f;
		}
	}
	for (c = 0; c < 8; c++)
	{
		gfloat x = (c & 1) ? hi[0] : lo[0];
		gfloat y = (c & 2) ? hi[1] : lo[1];
		gfloat z = (c & 4) ? hi[2] : lo[2];
		gfloat p[3];
		guint  k;

		/* raylib Matrix: fields named by column-major index. */
		p[0] = t->m0 * x + t->m4 * y + t->m8 * z + t->m12;
		p[1] = t->m1 * x + t->m5 * y + t->m9 * z + t->m13;
		p[2] = t->m2 * x + t->m6 * y + t->m10 * z + t->m14;
		for (k = 0; k < 3; k++)
		{
			if (!have || p[k] < mn[k])
				mn[k] = p[k];
			if (!have || p[k] > mx[k])
				mx[k] = p[k];
		}
		have = TRUE;
	}
	return have;
}

/* Takes the bounds from the models of a level unless set explicitly. */
static void
bounds_from_models (LrgLodGroup *self,
                    GPtrArray   *models)
{
	gboolean have = FALSE;
	gfloat   mn[3];
	gfloat   mx[3];
	guint    i;

	if (self->bounds_explicit || self->has_bounds || models == NULL)
		return;
	for (i = 0; i < models->len; i++)
	{
		Model *h = (Model *) grl_model_get_handle (GRL_MODEL (g_ptr_array_index (models, i)));
		gint   m;

		for (m = 0; h != NULL && m < h->meshCount; m++)
			have = grow_bounds_mesh (&h->meshes[m], &h->transform, mn, mx, have);
	}
	if (!have)
		return;
	memcpy (self->bounds_min, mn, sizeof mn);
	memcpy (self->bounds_max, mx, sizeof mx);
	self->has_bounds = TRUE;
}

LrgLodGroup *
lrg_lod_group_new (void)
{
	return g_object_new (LRG_TYPE_LOD_GROUP, NULL);
}

gint
lrg_lod_group_add_level_models (LrgLodGroup *self,
                                GPtrArray   *models,
                                gfloat       screen_size)
{
	Level *level;
	guint  i;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), -1);
	g_return_val_if_fail (models != NULL, -1);

	if (self->n_levels >= LRG_LOD_MAX_LEVELS)
		return -1;
	level = &self->levels[self->n_levels];
	level->models = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < models->len; i++)
	{
		GrlModel *m = g_ptr_array_index (models, i);

		g_return_val_if_fail (GRL_IS_MODEL (m), -1);
		g_ptr_array_add (level->models, g_object_ref (m));
	}
	level->screen_size = MAX (screen_size, 0.0f);
	level->error = 0.0f;
	self->n_levels++;

	bounds_from_models (self, level->models);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_LEVELS]);
	emit_changed (self);
	return (gint) self->n_levels - 1;
}

gint
lrg_lod_group_add_level (LrgLodGroup *self,
                         GrlModel    *model,
                         gfloat       screen_size)
{
	g_autoptr(GPtrArray) models = g_ptr_array_new ();

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), -1);
	g_return_val_if_fail (model == NULL || GRL_IS_MODEL (model), -1);

	if (model != NULL)
		g_ptr_array_add (models, model);
	return lrg_lod_group_add_level_models (self, models, screen_size);
}

void
lrg_lod_group_remove_all_levels (LrgLodGroup *self)
{
	guint i;

	g_return_if_fail (LRG_IS_LOD_GROUP (self));

	if (self->n_levels == 0)
		return;
	for (i = 0; i < self->n_levels; i++)
		g_clear_pointer (&self->levels[i].models, g_ptr_array_unref);
	self->n_levels = 0;
	if (!self->bounds_explicit)
		self->has_bounds = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_LEVELS]);
	emit_changed (self);
}

guint
lrg_lod_group_get_n_levels (LrgLodGroup *self)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0);
	return self->n_levels;
}

GrlModel *
lrg_lod_group_get_level_model (LrgLodGroup *self,
                               guint        level)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), NULL);

	if (level >= self->n_levels || self->levels[level].models->len == 0)
		return NULL;
	return g_ptr_array_index (self->levels[level].models, 0);
}

GPtrArray *
lrg_lod_group_get_level_models (LrgLodGroup *self,
                                guint        level)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), NULL);

	if (level >= self->n_levels)
		return NULL;
	return self->levels[level].models;
}

gint
lrg_lod_group_find_level_of_model (LrgLodGroup *self,
                                   GrlModel    *model)
{
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), -1);

	for (i = 0; model != NULL && i < self->n_levels; i++)
		for (j = 0; j < self->levels[i].models->len; j++)
			if (g_ptr_array_index (self->levels[i].models, j) == (gpointer) model)
				return (gint) i;
	return -1;
}

gfloat
lrg_lod_group_get_level_screen_size (LrgLodGroup *self,
                                     guint        level)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0.0f);
	g_return_val_if_fail (level < self->n_levels, 0.0f);
	return self->levels[level].screen_size;
}

void
lrg_lod_group_set_level_screen_size (LrgLodGroup *self,
                                     guint        level,
                                     gfloat       screen_size)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));
	g_return_if_fail (level < self->n_levels);

	screen_size = MAX (screen_size, 0.0f);
	if (self->levels[level].screen_size == screen_size)
		return;
	self->levels[level].screen_size = screen_size;
	emit_changed (self);
}

gint
lrg_lod_group_get_level_triangle_count (LrgLodGroup *self,
                                        guint        level)
{
	gint  total = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0);
	g_return_val_if_fail (level < self->n_levels, 0);

	for (i = 0; i < self->levels[level].models->len; i++)
	{
		Model *h = (Model *) grl_model_get_handle (GRL_MODEL (g_ptr_array_index (self->levels[level].models, i)));
		gint   m;

		for (m = 0; h != NULL && m < h->meshCount; m++)
			total += h->meshes[m].triangleCount;
	}
	return total;
}

gfloat
lrg_lod_group_get_level_error (LrgLodGroup *self,
                               guint        level)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0.0f);
	g_return_val_if_fail (level < self->n_levels, 0.0f);
	return self->levels[level].error;
}

/* --------------------------------------------------------------------------
 * Bounds
 * -------------------------------------------------------------------------- */

void
lrg_lod_group_set_bounds (LrgLodGroup  *self,
                          const gfloat *min,
                          const gfloat *max)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));
	g_return_if_fail (min != NULL && max != NULL);

	memcpy (self->bounds_min, min, sizeof (gfloat) * 3);
	memcpy (self->bounds_max, max, sizeof (gfloat) * 3);
	self->has_bounds = TRUE;
	self->bounds_explicit = TRUE;
	emit_changed (self);
}

gboolean
lrg_lod_group_get_bounds (LrgLodGroup *self,
                          gfloat      *out_min,
                          gfloat      *out_max)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), FALSE);

	if (!self->has_bounds)
		return FALSE;
	if (out_min != NULL)
		memcpy (out_min, self->bounds_min, sizeof (gfloat) * 3);
	if (out_max != NULL)
		memcpy (out_max, self->bounds_max, sizeof (gfloat) * 3);
	return TRUE;
}

gboolean
lrg_lod_group_get_bounding_sphere (LrgLodGroup *self,
                                   gfloat      *out_center,
                                   gfloat      *out_radius)
{
	gfloat d[3];
	guint  k;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), FALSE);

	if (!self->has_bounds)
		return FALSE;
	for (k = 0; k < 3; k++)
	{
		if (out_center != NULL)
			out_center[k] = 0.5f * (self->bounds_min[k] + self->bounds_max[k]);
		d[k] = self->bounds_max[k] - self->bounds_min[k];
	}
	if (out_radius != NULL)
		*out_radius = 0.5f * sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Settings
 * -------------------------------------------------------------------------- */

gfloat
lrg_lod_group_get_hysteresis (LrgLodGroup *self)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0.0f);
	return self->hysteresis;
}

void
lrg_lod_group_set_hysteresis (LrgLodGroup *self,
                              gfloat       hysteresis)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));

	hysteresis = CLAMP (hysteresis, 0.0f, 0.9f);
	if (self->hysteresis == hysteresis)
		return;
	self->hysteresis = hysteresis;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HYSTERESIS]);
}

gfloat
lrg_lod_group_get_bias (LrgLodGroup *self)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 1.0f);
	return self->bias;
}

void
lrg_lod_group_set_bias (LrgLodGroup *self,
                        gfloat       bias)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));
	g_return_if_fail (bias > 0.0f);

	if (self->bias == bias)
		return;
	self->bias = bias;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BIAS]);
}

gint
lrg_lod_group_get_force_level (LrgLodGroup *self)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), -1);
	return self->force_level;
}

void
lrg_lod_group_set_force_level (LrgLodGroup *self,
                               gint         level)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));

	level = CLAMP (level, -1, LRG_LOD_MAX_LEVELS - 1);
	if (self->force_level == level)
		return;
	self->force_level = level;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORCE_LEVEL]);
}

gfloat
lrg_lod_group_get_fade_duration (LrgLodGroup *self)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), 0.0f);
	return self->fade_duration;
}

void
lrg_lod_group_set_fade_duration (LrgLodGroup *self,
                                 gfloat       seconds)
{
	g_return_if_fail (LRG_IS_LOD_GROUP (self));

	seconds = MAX (seconds, 0.0f);
	if (self->fade_duration == seconds)
		return;
	self->fade_duration = seconds;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FADE_DURATION]);
}

/* --------------------------------------------------------------------------
 * Selection
 * -------------------------------------------------------------------------- */

gint
lrg_lod_group_select_level (LrgLodGroup *self,
                            gfloat       screen_size,
                            gint         current_level)
{
	gint   n;
	gint   raw = LRG_LOD_LEVEL_CULLED;
	gint   cur;
	gint   r;
	gint   i;
	gfloat e;
	gfloat h;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), LRG_LOD_LEVEL_CULLED);

	n = (gint) self->n_levels;
	if (n == 0)
		return LRG_LOD_LEVEL_CULLED;
	if (self->force_level >= 0)
		return MIN (self->force_level, n - 1);

	/* 1. The level the biased size asks for, ignoring history. */
	e = screen_size * self->bias * global_bias;
	for (i = 0; i < n; i++)
		if (e >= self->levels[i].screen_size)
		{
			raw = i;
			break;
		}
	if (current_level < LRG_LOD_LEVEL_CULLED || current_level >= n)
		return raw;

	/* 2. Hysteresis: culled counts as level n (coarser than any). */
	h = self->hysteresis;
	cur = (current_level == LRG_LOD_LEVEL_CULLED) ? n : current_level;
	r = (raw == LRG_LOD_LEVEL_CULLED) ? n : raw;
	if (r > cur)
	{
		/* Coarser only once below the current threshold's lower band. */
		if (e >= self->levels[cur].screen_size * (1.0f - h))
			return current_level;
		return raw;
	}
	if (r < cur)
	{
		/* Finer only once above a finer threshold's upper band. */
		for (i = 0; i < cur; i++)
			if (e >= self->levels[i].screen_size * (1.0f + h))
				return i;
		return current_level;
	}
	return raw;
}

gboolean
lrg_lod_group_update_state (LrgLodGroup *self,
                            LrgLodState *state,
                            gfloat       screen_size,
                            gfloat       delta)
{
	gint level;

	g_return_val_if_fail (LRG_IS_LOD_GROUP (self), FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	state->screen_size = screen_size * self->bias * global_bias;
	if (!state->initialized)
	{
		state->level = lrg_lod_group_select_level (self, screen_size, LRG_LOD_LEVEL_NONE);
		state->previous_level = LRG_LOD_LEVEL_CULLED;
		state->fade = 1.0f;
		state->initialized = TRUE;
		return TRUE;
	}

	/* Advance a running fade first, so a switch restarts it at 0. */
	if (state->fade < 1.0f)
	{
		if (self->fade_duration > 0.0f && delta > 0.0f)
			state->fade = MIN (1.0f, state->fade + delta / self->fade_duration);
		else if (self->fade_duration <= 0.0f)
			state->fade = 1.0f;
	}

	level = lrg_lod_group_select_level (self, screen_size, state->level);
	if (level == state->level)
		return FALSE;

	state->previous_level = state->level;
	state->level = level;
	state->fade = (self->fade_duration > 0.0f) ? 0.0f : 1.0f;
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Generation
 * -------------------------------------------------------------------------- */

/* Default threshold of @level in an @n-level chain. */
static gfloat
default_screen_size (guint level,
                     guint n)
{
	if (level + 1 >= n)
		return 0.0f;
	return (gfloat) pow (0.5, (gdouble) (level + 1));
}

/*
 * make_part:
 *
 * One single-mesh GrlModel around a simplified mesh, with the source
 * mesh's material (maps, shader and params copied: the textures and the
 * shader stay owned by the source model, which the group keeps alive)
 * and the source model transform.
 */
static GrlModel *
make_part (const Model  *src,
           gint          mesh_index,
           Mesh         *simplified,
           GError      **error)
{
	g_autoptr(GrlMesh) mesh = NULL;
	GrlModel          *part;
	Model             *h;
	const Material    *sm;

	mesh = lrg_mesh_simplify_wrap_raylib_mesh (simplified, error);
	if (mesh == NULL)
		return NULL;
	part = grl_model_new_from_mesh (mesh);
	h = (Model *) grl_model_get_handle (part);
	h->transform = src->transform;
	if (src->materials != NULL && src->meshMaterial != NULL && h->materialCount > 0)
	{
		sm = &src->materials[src->meshMaterial[mesh_index]];
		h->materials[0].shader = sm->shader;
		if (h->materials[0].maps != NULL && sm->maps != NULL)
			memcpy (h->materials[0].maps, sm->maps, sizeof (MaterialMap) * LOD_MATERIAL_MAPS);
		memcpy (h->materials[0].params, sm->params, sizeof (sm->params));
	}
	return part;
}

LrgLodGroup *
lrg_lod_group_new_from_model (GrlModel         *model,
                              LrgMeshSimplify  *simplify,
                              guint             n_levels,
                              const gfloat     *ratios,
                              guint             n_ratios,
                              const gfloat     *screen_sizes,
                              guint             n_screen_sizes,
                              GError          **error)
{
	g_autoptr(LrgLodGroup)     self = NULL;
	g_autoptr(LrgMeshSimplify) s = NULL;
	Model                     *src;
	guint                      i;

	g_return_val_if_fail (GRL_IS_MODEL (model), NULL);
	g_return_val_if_fail (n_levels >= 1 && n_levels <= LRG_LOD_MAX_LEVELS, NULL);
	g_return_val_if_fail (simplify == NULL || LRG_IS_MESH_SIMPLIFY (simplify), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	self = lrg_lod_group_new ();
	lrg_lod_group_add_level (self, model,
	                         n_screen_sizes > 0 && screen_sizes != NULL ? screen_sizes[0]
	                                                                    : default_screen_size (0, n_levels));
	if (n_levels == 1)
		return g_steal_pointer (&self);

	src = (Model *) grl_model_get_handle (model);
	if (src == NULL || src->meshCount <= 0)
	{
		g_set_error_literal (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
		                     "The model has no meshes");
		return NULL;
	}
	if (grl_model_get_bone_count (model) > 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "LOD generation does not support skinned models");
		return NULL;
	}

	/* A private simplifier so the caller's ratio is left alone. */
	s = lrg_mesh_simplify_new ();
	if (simplify != NULL)
	{
		lrg_mesh_simplify_set_target_error (s, lrg_mesh_simplify_get_target_error (simplify));
		lrg_mesh_simplify_set_lock_border (s, lrg_mesh_simplify_get_lock_border (simplify));
		lrg_mesh_simplify_set_lock_seams (s, lrg_mesh_simplify_get_lock_seams (simplify));
	}
	else
		lrg_mesh_simplify_set_target_error (s, 0.05f);

	/* Every level is simplified from the original (errors do not stack). */
	for (i = 1; i < n_levels; i++)
	{
		g_autoptr(GPtrArray) parts = g_ptr_array_new_with_free_func (g_object_unref);
		gfloat               ratio;
		gfloat               ss;
		gfloat               level_error = 0.0f;
		gint                 m;
		gint                 index;

		ratio = (ratios != NULL && i - 1 < n_ratios) ? ratios[i - 1] : (gfloat) pow (0.5, (gdouble) i);
		ss = (screen_sizes != NULL && i < n_screen_sizes) ? screen_sizes[i] : default_screen_size (i, n_levels);
		lrg_mesh_simplify_set_target_ratio (s, ratio);

		for (m = 0; m < src->meshCount; m++)
		{
			Mesh      out;
			GrlModel *part;

			if (!lrg_mesh_simplify_raylib_mesh (s, &src->meshes[m], &out, error))
				return NULL;
			level_error = MAX (level_error, lrg_mesh_simplify_get_result_error (s));
			part = make_part (src, m, &out, error);
			if (part == NULL)
				return NULL;
			g_ptr_array_add (parts, part);
		}
		index = lrg_lod_group_add_level_models (self, parts, ss);
		if (index >= 0)
			self->levels[index].error = level_error;
	}
	return g_steal_pointer (&self);
}

/* Parses a list setting of @meta into @out (at most @max entries). */
static gboolean
meta_float_list (gchar   *text,
                 gfloat  *out,
                 guint    max,
                 guint   *n_out,
                 GError **error)
{
	g_autofree gchar *owned = text;
	g_autoptr(GArray) list = NULL;
	guint             i;

	*n_out = 0;
	list = lrg_asset_meta_parse_float_list (owned, error);
	if (list == NULL)
		return FALSE;
	for (i = 0; i < list->len && i < max; i++)
		out[i] = g_array_index (list, gfloat, i);
	*n_out = MIN (list->len, max);
	return TRUE;
}

void
lrg_lod_group_apply_meta (LrgLodGroup  *self,
                          LrgAssetMeta *meta)
{
	gfloat sizes[LRG_LOD_MAX_LEVELS];
	guint  n_sizes = 0;
	guint  i;

	g_return_if_fail (LRG_IS_LOD_GROUP (self));
	g_return_if_fail (LRG_IS_ASSET_META (meta));

	if (meta_float_list (lrg_asset_meta_get_model_lod_screen_sizes (meta), sizes, LRG_LOD_MAX_LEVELS,
	                     &n_sizes, NULL))
	{
		for (i = 0; i < self->n_levels; i++)
			lrg_lod_group_set_level_screen_size (self, i, i < n_sizes ? sizes[i]
			                                                          : default_screen_size (i, self->n_levels));
	}
	lrg_lod_group_set_hysteresis (self, (gfloat) lrg_asset_meta_get_model_lod_hysteresis (meta));
	lrg_lod_group_set_fade_duration (self, (gfloat) lrg_asset_meta_get_model_lod_fade_duration (meta));
}

LrgLodGroup *
lrg_lod_group_new_from_model_with_meta (GrlModel      *model,
                                        LrgAssetMeta  *meta,
                                        GError       **error)
{
	g_autoptr(LrgMeshSimplify) s = NULL;
	LrgLodGroup               *self;
	gfloat                     ratios[LRG_LOD_MAX_LEVELS];
	gfloat                     sizes[LRG_LOD_MAX_LEVELS];
	guint                      n_ratios = 0;
	guint                      n_sizes = 0;
	guint                      n_levels;

	g_return_val_if_fail (GRL_IS_MODEL (model), NULL);
	g_return_val_if_fail (meta == NULL || LRG_IS_ASSET_META (meta), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (meta == NULL)
		return lrg_lod_group_new_from_model (model, NULL, 1, NULL, 0, NULL, 0, error);

	n_levels = lrg_asset_meta_get_model_lod_levels (meta);
	if (!meta_float_list (lrg_asset_meta_get_model_lod_ratios (meta), ratios, LRG_LOD_MAX_LEVELS, &n_ratios,
	                      error) ||
	    !meta_float_list (lrg_asset_meta_get_model_lod_screen_sizes (meta), sizes, LRG_LOD_MAX_LEVELS,
	                      &n_sizes, error))
		return NULL;

	s = lrg_mesh_simplify_new ();
	lrg_mesh_simplify_set_target_error (s, (gfloat) lrg_asset_meta_get_model_lod_max_error (meta));
	lrg_mesh_simplify_set_lock_border (s, lrg_asset_meta_get_model_lod_lock_border (meta));

	self = lrg_lod_group_new_from_model (model, s, n_levels, ratios, n_ratios, sizes, n_sizes, error);
	if (self == NULL)
		return NULL;
	lrg_lod_group_set_hysteresis (self, (gfloat) lrg_asset_meta_get_model_lod_hysteresis (meta));
	lrg_lod_group_set_fade_duration (self, (gfloat) lrg_asset_meta_get_model_lod_fade_duration (meta));
	return self;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_lod_group_dispose (GObject *object)
{
	LrgLodGroup *self = LRG_LOD_GROUP (object);
	guint        i;

	/* Generated parts go first: they borrow the source model's textures. */
	for (i = self->n_levels; i > 0; i--)
		g_clear_pointer (&self->levels[i - 1].models, g_ptr_array_unref);
	self->n_levels = 0;

	G_OBJECT_CLASS (lrg_lod_group_parent_class)->dispose (object);
}

static void
lrg_lod_group_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgLodGroup *self = LRG_LOD_GROUP (object);

	switch (prop_id)
	{
	case PROP_N_LEVELS:
		g_value_set_uint (value, self->n_levels);
		break;
	case PROP_HYSTERESIS:
		g_value_set_float (value, self->hysteresis);
		break;
	case PROP_BIAS:
		g_value_set_float (value, self->bias);
		break;
	case PROP_FORCE_LEVEL:
		g_value_set_int (value, self->force_level);
		break;
	case PROP_FADE_DURATION:
		g_value_set_float (value, self->fade_duration);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lod_group_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgLodGroup *self = LRG_LOD_GROUP (object);

	switch (prop_id)
	{
	case PROP_HYSTERESIS:
		lrg_lod_group_set_hysteresis (self, g_value_get_float (value));
		break;
	case PROP_BIAS:
		lrg_lod_group_set_bias (self, g_value_get_float (value));
		break;
	case PROP_FORCE_LEVEL:
		lrg_lod_group_set_force_level (self, g_value_get_int (value));
		break;
	case PROP_FADE_DURATION:
		lrg_lod_group_set_fade_duration (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lod_group_class_init (LrgLodGroupClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_lod_group_dispose;
	object_class->get_property = lrg_lod_group_get_property;
	object_class->set_property = lrg_lod_group_set_property;

	/**
	 * LrgLodGroup:n-levels:
	 *
	 * Number of levels.
	 */
	properties[PROP_N_LEVELS] =
		g_param_spec_uint ("n-levels", "Levels", "Number of levels",
		                   0, LRG_LOD_MAX_LEVELS, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLodGroup:hysteresis:
	 *
	 * Relative band around each threshold inside which the level stays.
	 */
	properties[PROP_HYSTERESIS] =
		g_param_spec_float ("hysteresis", "Hysteresis", "Relative band around thresholds",
		                    0.0f, 0.9f, 0.1f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLodGroup:bias:
	 *
	 * Screen-size multiplier (with lrg_lod_get_global_bias()).
	 */
	properties[PROP_BIAS] =
		g_param_spec_float ("bias", "Bias", "Screen-size multiplier",
		                    0.0001f, 10000.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLodGroup:force-level:
	 *
	 * Level used regardless of screen size, or -1.
	 */
	properties[PROP_FORCE_LEVEL] =
		g_param_spec_int ("force-level", "Force level", "Level to always use, or -1",
		                  -1, LRG_LOD_MAX_LEVELS - 1, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLodGroup:fade-duration:
	 *
	 * Seconds of dithered cross-fade on a level change (0 = pop).
	 */
	properties[PROP_FADE_DURATION] =
		g_param_spec_float ("fade-duration", "Fade duration", "Cross-fade seconds",
		                    0.0f, 60.0f, 0.25f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgLodGroup::changed:
	 * @self: the group
	 *
	 * Emitted when levels are added or removed, a threshold changes or
	 * the bounds are set.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_lod_group_init (LrgLodGroup *self)
{
	self->hysteresis = 0.1f;
	self->bias = 1.0f;
	self->force_level = -1;
	self->fade_duration = 0.25f;
}
