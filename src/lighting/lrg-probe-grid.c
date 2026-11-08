/* lrg-probe-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProbeGrid: a lattice of L2 SH irradiance probes over a box, the
 * `.rprobes` format, CPU trilinear sampling and the RGBA16F 3D texture
 * the lit shader reads (LRG_BAKED_GLSL).  See lrg-probe-grid.h and
 * docs/modules/lighting/probe-grid.org.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <yaml-glib.h>

#include "lrg-probe-grid.h"
#include "lrg-environment.h"
#include "lrg-baked-glsl-private.h"

/* floats of one padded probe on the GPU: 27 coefficients + validity */
#define PADDED_FLOATS (28)
/* RGBA groups of one padded probe */
#define GROUPS (7)

struct _LrgProbeGrid
{
	GObject   parent_instance;

	gfloat    min[3];
	gfloat    max[3];
	guint     n[3];
	gfloat   *sh;         /* 27 x probes, as stored */
	guint8   *valid;      /* one flag per probe */
	gfloat   *resolved;   /* 27 x probes, invalid probes filled; NULL = stale */
	gboolean  any_valid;  /* of the last resolve */
	gint      n_valid;    /* cached count, -1 = stale */
	gfloat    intensity;
	gchar    *path;

	guint     texture;    /* GL 3D texture, 0 = none */
	gboolean  gpu_dirty;
};

G_DEFINE_FINAL_TYPE (LrgProbeGrid, lrg_probe_grid, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_INTENSITY,
	PROP_N_PROBES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Error domain and enum type
 * ========================================================================== */

G_DEFINE_QUARK (lrg-probe-grid-error-quark, lrg_probe_grid_error)

GType
lrg_probe_grid_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PROBE_GRID_ERROR_FAILED, "LRG_PROBE_GRID_ERROR_FAILED", "failed" },
			{ LRG_PROBE_GRID_ERROR_IO, "LRG_PROBE_GRID_ERROR_IO", "io" },
			{ LRG_PROBE_GRID_ERROR_FORMAT, "LRG_PROBE_GRID_ERROR_FORMAT", "format" },
			{ LRG_PROBE_GRID_ERROR_VERSION, "LRG_PROBE_GRID_ERROR_VERSION", "version" },
			{ LRG_PROBE_GRID_ERROR_GPU, "LRG_PROBE_GRID_ERROR_GPU", "gpu" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgProbeGridError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * GL 3D textures (rlgl has no 3D texture API): entry points through
 * rlGetProcAddress, looked up once per process.
 * ========================================================================== */

#define GL_TEXTURE_3D_         (0x806F)
#define GL_TEXTURE0_           (0x84C0)
#define GL_RGBA16F_            (0x881A)
#define GL_RGBA_               (0x1908)
#define GL_FLOAT_              (0x1406)
#define GL_TEXTURE_MAG_FILTER_ (0x2800)
#define GL_TEXTURE_MIN_FILTER_ (0x2801)
#define GL_TEXTURE_WRAP_S_     (0x2802)
#define GL_TEXTURE_WRAP_T_     (0x2803)
#define GL_TEXTURE_WRAP_R_     (0x8072)
#define GL_LINEAR_             (0x2601)
#define GL_CLAMP_TO_EDGE_      (0x812F)
#define GL_UNPACK_ALIGNMENT_   (0x0CF5)

typedef void (*GlGenTextures) (gint n, guint *ids);
typedef void (*GlDeleteTextures) (gint n, const guint *ids);
typedef void (*GlBindTexture) (guint target, guint id);
typedef void (*GlActiveTexture) (guint unit);
typedef void (*GlTexParameteri) (guint target, guint pname, gint param);
typedef void (*GlPixelStorei) (guint pname, gint param);
typedef void (*GlTexImage3D) (guint target, gint level, gint internal, gint w, gint h, gint d,
                              gint border, guint format, guint type, const void *data);

typedef struct
{
	gboolean         loaded;
	GlGenTextures    gen_textures;
	GlDeleteTextures delete_textures;
	GlBindTexture    bind_texture;
	GlActiveTexture  active_texture;
	GlTexParameteri  tex_parameteri;
	GlPixelStorei    pixel_storei;
	GlTexImage3D     tex_image_3d;
} Gl3D;

static Gl3D gl3d;

/* resolves the entry points; FALSE without a context or a function */
static gboolean
gl3d_load (void)
{
	if (!IsWindowReady ())
		return FALSE;
	if (!gl3d.loaded)
	{
		gl3d.gen_textures = (GlGenTextures)rlGetProcAddress ("glGenTextures");
		gl3d.delete_textures = (GlDeleteTextures)rlGetProcAddress ("glDeleteTextures");
		gl3d.bind_texture = (GlBindTexture)rlGetProcAddress ("glBindTexture");
		gl3d.active_texture = (GlActiveTexture)rlGetProcAddress ("glActiveTexture");
		gl3d.tex_parameteri = (GlTexParameteri)rlGetProcAddress ("glTexParameteri");
		gl3d.pixel_storei = (GlPixelStorei)rlGetProcAddress ("glPixelStorei");
		gl3d.tex_image_3d = (GlTexImage3D)rlGetProcAddress ("glTexImage3D");
		gl3d.loaded = TRUE;
	}
	return gl3d.gen_textures != NULL && gl3d.delete_textures != NULL && gl3d.bind_texture != NULL
	       && gl3d.active_texture != NULL && gl3d.tex_parameteri != NULL && gl3d.pixel_storei != NULL
	       && gl3d.tex_image_3d != NULL;
}

void
lrg_baked_bind_texture3d_private (gint  unit,
                                  guint texture)
{
	if (!gl3d_load ())
		return;
	gl3d.active_texture (GL_TEXTURE0_ + (guint)unit);
	gl3d.bind_texture (GL_TEXTURE_3D_, texture);
	gl3d.active_texture (GL_TEXTURE0_);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static guint
probe_count (LrgProbeGrid *self)
{
	return self->n[0] * self->n[1] * self->n[2];
}

/* data changed: drop the resolved copy, mark the texture stale, signal */
static void
mark_changed (LrgProbeGrid *self)
{
	g_clear_pointer (&self->resolved, g_free);
	self->n_valid = -1;
	self->gpu_dirty = TRUE;
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* (re)allocates zeroed, invalid storage for the current resolution */
static void
realloc_storage (LrgProbeGrid *self)
{
	guint count = probe_count (self);

	g_free (self->sh);
	g_free (self->valid);
	self->sh = g_new0 (gfloat, (gsize)count * LRG_PROBE_GRID_SH_FLOATS);
	self->valid = g_new0 (guint8, count);
	g_clear_pointer (&self->resolved, g_free);
}

/*
 * resolve:
 * Builds self->resolved: valid probes as stored; each invalid probe
 * takes the average of its already-filled 6-neighbours, wave after wave
 * outward from the valid ones (a breadth-first flood), so every probe
 * connected to a valid one gets a value.  With no valid probe at all
 * everything stays zero and any_valid is FALSE.
 */
static void
resolve (LrgProbeGrid *self)
{
	guint     count = probe_count (self);
	guint8   *filled;
	guint8   *next;
	gboolean  progress = TRUE;
	guint     i;

	if (self->resolved != NULL)
		return;
	self->resolved = g_memdup2 (self->sh, (gsize)count * LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));
	filled = g_memdup2 (self->valid, count);
	next = g_new (guint8, count);
	self->any_valid = FALSE;
	for (i = 0; i < count; i++)
		if (filled[i])
			self->any_valid = TRUE;
	if (!self->any_valid)
		memset (self->resolved, 0, (gsize)count * LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));

	while (self->any_valid && progress)
	{
		guint x, y, z;

		progress = FALSE;
		memcpy (next, filled, count);
		for (z = 0; z < self->n[2]; z++)
			for (y = 0; y < self->n[1]; y++)
				for (x = 0; x < self->n[0]; x++)
				{
					static const gint d[6][3] = {
						{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
					};
					guint   idx = x + self->n[0] * (y + self->n[1] * z);
					gfloat  acc[LRG_PROBE_GRID_SH_FLOATS];
					guint   used = 0;
					guint   k, c;

					if (filled[idx])
						continue;
					memset (acc, 0, sizeof acc);
					for (k = 0; k < 6; k++)
					{
						gint  nx = (gint)x + d[k][0];
						gint  ny = (gint)y + d[k][1];
						gint  nz = (gint)z + d[k][2];
						guint nidx;

						if (nx < 0 || ny < 0 || nz < 0 || nx >= (gint)self->n[0] || ny >= (gint)self->n[1]
						    || nz >= (gint)self->n[2])
							continue;
						nidx = (guint)nx + self->n[0] * ((guint)ny + self->n[1] * (guint)nz);
						if (!filled[nidx])
							continue;
						for (c = 0; c < LRG_PROBE_GRID_SH_FLOATS; c++)
							acc[c] += self->resolved[(gsize)nidx * LRG_PROBE_GRID_SH_FLOATS + c];
						used++;
					}
					if (used == 0)
						continue;
					for (c = 0; c < LRG_PROBE_GRID_SH_FLOATS; c++)
						self->resolved[(gsize)idx * LRG_PROBE_GRID_SH_FLOATS + c] = acc[c] / (gfloat)used;
					next[idx] = 1;
					progress = TRUE;
				}
		memcpy (filled, next, count);
	}
	g_free (filled);
	g_free (next);
}

/* the lattice coordinate (0 .. n - 1) of world @p on axis @a, clamped */
static gfloat
lattice_coord (LrgProbeGrid *self,
               const gfloat *p,
               guint         a)
{
	gfloat ext = self->max[a] - self->min[a];
	gfloat t;

	if (self->n[a] <= 1 || ext <= 1e-6f)
		return 0.0f;
	t = CLAMP ((p[a] - self->min[a]) / ext, 0.0f, 1.0f);
	return t * (gfloat)(self->n[a] - 1);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_probe_grid_finalize (GObject *object)
{
	LrgProbeGrid *self = LRG_PROBE_GRID (object);

	if (self->texture != 0 && gl3d_load ())
		gl3d.delete_textures (1, &self->texture);
	g_free (self->sh);
	g_free (self->valid);
	g_free (self->resolved);
	g_free (self->path);

	G_OBJECT_CLASS (lrg_probe_grid_parent_class)->finalize (object);
}

static void
lrg_probe_grid_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgProbeGrid *self = LRG_PROBE_GRID (object);

	switch (prop_id)
	{
	case PROP_INTENSITY:
		g_value_set_float (value, self->intensity);
		break;
	case PROP_N_PROBES:
		g_value_set_uint (value, probe_count (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_probe_grid_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgProbeGrid *self = LRG_PROBE_GRID (object);

	switch (prop_id)
	{
	case PROP_INTENSITY:
		lrg_probe_grid_set_intensity (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_probe_grid_class_init (LrgProbeGridClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_probe_grid_finalize;
	object_class->get_property = lrg_probe_grid_get_property;
	object_class->set_property = lrg_probe_grid_set_property;

	/**
	 * LrgProbeGrid:intensity:
	 *
	 * Multiplier on every probe (CPU samples and the shader).
	 */
	properties[PROP_INTENSITY] =
		g_param_spec_float ("intensity", "Intensity", "Multiplier on every probe",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProbeGrid:n-probes:
	 *
	 * nx x ny x nz.
	 */
	properties[PROP_N_PROBES] =
		g_param_spec_uint ("n-probes", "Probes", "Number of probes",
		                   0, G_MAXUINT, 8,
		                   G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgProbeGrid::changed:
	 * @self: the grid
	 *
	 * Emitted after the box, the resolution or probe data changed.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_probe_grid_init (LrgProbeGrid *self)
{
	guint a;

	for (a = 0; a < 3; a++)
	{
		self->min[a] = -1.0f;
		self->max[a] = 1.0f;
		self->n[a] = 2;
	}
	self->intensity = 1.0f;
	self->gpu_dirty = TRUE;
	self->n_valid = -1;
	realloc_storage (self);
}

LrgProbeGrid *
lrg_probe_grid_new (void)
{
	return g_object_new (LRG_TYPE_PROBE_GRID, NULL);
}

LrgProbeGrid *
lrg_probe_grid_new_with_bounds (const gfloat *min,
                                const gfloat *max,
                                guint         nx,
                                guint         ny,
                                guint         nz)
{
	LrgProbeGrid *self;

	g_return_val_if_fail (min != NULL && max != NULL, NULL);

	self = lrg_probe_grid_new ();
	lrg_probe_grid_set_bounds (self, min, max);
	lrg_probe_grid_set_resolution (self, nx, ny, nz);
	return self;
}

const gchar *
lrg_probe_grid_get_path (LrgProbeGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), NULL);
	return self->path;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

void
lrg_probe_grid_set_bounds (LrgProbeGrid *self,
                           const gfloat *min,
                           const gfloat *max)
{
	guint a;

	g_return_if_fail (LRG_IS_PROBE_GRID (self));
	g_return_if_fail (min != NULL && max != NULL);

	for (a = 0; a < 3; a++)
	{
		self->min[a] = MIN (min[a], max[a]);
		self->max[a] = MAX (min[a], max[a]);
	}
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_probe_grid_get_bounds (LrgProbeGrid *self,
                           gfloat       *out_min,
                           gfloat       *out_max)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	if (out_min != NULL)
		memcpy (out_min, self->min, sizeof self->min);
	if (out_max != NULL)
		memcpy (out_max, self->max, sizeof self->max);
}

void
lrg_probe_grid_set_resolution (LrgProbeGrid *self,
                               guint         nx,
                               guint         ny,
                               guint         nz)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	self->n[0] = CLAMP (nx, 1u, (guint)LRG_PROBE_GRID_MAX_RESOLUTION);
	self->n[1] = CLAMP (ny, 1u, (guint)LRG_PROBE_GRID_MAX_RESOLUTION);
	self->n[2] = CLAMP (nz, 1u, (guint)LRG_PROBE_GRID_MAX_RESOLUTION);
	realloc_storage (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PROBES]);
	mark_changed (self);
}

void
lrg_probe_grid_get_resolution (LrgProbeGrid *self,
                               guint        *out_nx,
                               guint        *out_ny,
                               guint        *out_nz)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	if (out_nx != NULL)
		*out_nx = self->n[0];
	if (out_ny != NULL)
		*out_ny = self->n[1];
	if (out_nz != NULL)
		*out_nz = self->n[2];
}

guint
lrg_probe_grid_get_n_probes (LrgProbeGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), 0);
	return probe_count (self);
}

guint
lrg_probe_grid_get_index (LrgProbeGrid *self,
                          guint         x,
                          guint         y,
                          guint         z)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), G_MAXUINT);

	if (x >= self->n[0] || y >= self->n[1] || z >= self->n[2])
		return G_MAXUINT;
	return x + self->n[0] * (y + self->n[1] * z);
}

gboolean
lrg_probe_grid_get_probe_position (LrgProbeGrid *self,
                                   guint         index,
                                   gfloat       *out_position)
{
	guint c[3];
	guint a;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (out_position != NULL, FALSE);

	if (index >= probe_count (self))
		return FALSE;
	c[0] = index % self->n[0];
	c[1] = (index / self->n[0]) % self->n[1];
	c[2] = index / (self->n[0] * self->n[1]);
	for (a = 0; a < 3; a++)
	{
		if (self->n[a] <= 1)
			out_position[a] = 0.5f * (self->min[a] + self->max[a]);
		else
			out_position[a] = self->min[a]
			                  + (self->max[a] - self->min[a]) * (gfloat)c[a] / (gfloat)(self->n[a] - 1);
	}
	return TRUE;
}

/* ==========================================================================
 * Probe data
 * ========================================================================== */

gboolean
lrg_probe_grid_set_probe (LrgProbeGrid *self,
                          guint         index,
                          const gfloat *sh,
                          gboolean      valid)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (sh != NULL, FALSE);

	if (index >= probe_count (self))
		return FALSE;
	memcpy (self->sh + (gsize)index * LRG_PROBE_GRID_SH_FLOATS, sh,
	        LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));
	self->valid[index] = valid ? 1 : 0;
	mark_changed (self);
	return TRUE;
}

gboolean
lrg_probe_grid_get_probe (LrgProbeGrid *self,
                          guint         index,
                          gfloat       *out_sh)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);

	if (index >= probe_count (self))
		return FALSE;
	if (out_sh != NULL)
		memcpy (out_sh, self->sh + (gsize)index * LRG_PROBE_GRID_SH_FLOATS,
		        LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));
	return self->valid[index] != 0;
}

void
lrg_probe_grid_set_valid (LrgProbeGrid *self,
                          guint         index,
                          gboolean      valid)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	if (index >= probe_count (self))
		return;
	self->valid[index] = valid ? 1 : 0;
	mark_changed (self);
}

guint
lrg_probe_grid_get_n_valid (LrgProbeGrid *self)
{
	guint count, i, n = 0;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), 0);

	if (self->n_valid >= 0)
		return (guint)self->n_valid;
	count = probe_count (self);
	for (i = 0; i < count; i++)
		if (self->valid[i])
			n++;
	self->n_valid = (gint)n;
	return n;
}

gfloat *
lrg_probe_grid_get_data (LrgProbeGrid *self,
                         gsize        *n_floats)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), NULL);

	if (n_floats != NULL)
		*n_floats = (gsize)probe_count (self) * LRG_PROBE_GRID_SH_FLOATS;
	return self->sh;
}

guint8 *
lrg_probe_grid_get_validity (LrgProbeGrid *self,
                             guint        *n_probes)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), NULL);

	if (n_probes != NULL)
		*n_probes = probe_count (self);
	return self->valid;
}

void
lrg_probe_grid_notify_changed (LrgProbeGrid *self)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));
	mark_changed (self);
}

/* ==========================================================================
 * Sampling
 * ========================================================================== */

gboolean
lrg_probe_grid_contains (LrgProbeGrid *self,
                         const gfloat *position)
{
	guint a;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);

	for (a = 0; a < 3; a++)
		if (position[a] < self->min[a] || position[a] > self->max[a])
			return FALSE;
	return TRUE;
}

gboolean
lrg_probe_grid_sample_sh (LrgProbeGrid *self,
                          const gfloat *position,
                          gfloat       *out_sh)
{
	gfloat f[3], t[3];
	guint  i0[3], i1[3];
	guint  a, corner, c;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (position != NULL && out_sh != NULL, FALSE);

	memset (out_sh, 0, LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));
	resolve (self);
	if (!self->any_valid)
		return FALSE;

	/* the cell of the lattice and the position inside it */
	for (a = 0; a < 3; a++)
	{
		f[a] = lattice_coord (self, position, a);
		i0[a] = (guint)floorf (f[a]);
		if (i0[a] >= self->n[a])
			i0[a] = self->n[a] - 1;
		i1[a] = MIN (i0[a] + 1, self->n[a] - 1);
		t[a] = f[a] - (gfloat)i0[a];
	}

	/* trilinear blend of the eight resolved corners */
	for (corner = 0; corner < 8; corner++)
	{
		guint  x = (corner & 1) ? i1[0] : i0[0];
		guint  y = (corner & 2) ? i1[1] : i0[1];
		guint  z = (corner & 4) ? i1[2] : i0[2];
		gfloat w = ((corner & 1) ? t[0] : 1.0f - t[0]) * ((corner & 2) ? t[1] : 1.0f - t[1])
		           * ((corner & 4) ? t[2] : 1.0f - t[2]);
		const gfloat *src;

		if (w <= 0.0f)
			continue;
		src = self->resolved + (gsize)(x + self->n[0] * (y + self->n[1] * z)) * LRG_PROBE_GRID_SH_FLOATS;
		for (c = 0; c < LRG_PROBE_GRID_SH_FLOATS; c++)
			out_sh[c] += src[c] * w;
	}
	for (c = 0; c < LRG_PROBE_GRID_SH_FLOATS; c++)
		out_sh[c] *= self->intensity;
	return TRUE;
}

gboolean
lrg_probe_grid_sample (LrgProbeGrid *self,
                       const gfloat *position,
                       const gfloat *normal,
                       gfloat       *out_rgb)
{
	gfloat sh[LRG_PROBE_GRID_SH_FLOATS];

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (position != NULL && normal != NULL && out_rgb != NULL, FALSE);

	if (!lrg_probe_grid_sample_sh (self, position, sh))
	{
		out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
		return FALSE;
	}
	lrg_environment_sh_irradiance (sh, normal, out_rgb);
	return TRUE;
}

void
lrg_probe_grid_set_intensity (LrgProbeGrid *self,
                              gfloat        intensity)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	intensity = MAX (intensity, 0.0f);
	if (self->intensity == intensity)
		return;
	self->intensity = intensity;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_probe_grid_get_intensity (LrgProbeGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), 1.0f);
	return self->intensity;
}

/* ==========================================================================
 * Files
 *
 * version: 1
 * bounds: { min: [x, y, z], max: [x, y, z] }
 * resolution: [nx, ny, nz]
 * intensity: 1
 * encoding: base64-f32le
 * sh: <base64 of 27 x probes little-endian float32>
 * valid: <base64 of one byte per probe>
 * ========================================================================== */

/* a 3-number YAML sequence */
static YamlNode *
vec_node (const gfloat *v,
          guint         n)
{
	YamlNode *node = yaml_node_new_sequence (NULL);
	guint     i;

	for (i = 0; i < n; i++)
	{
		gchar               buf[G_ASCII_DTOSTR_BUF_SIZE];
		g_autoptr(YamlNode) e = yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.9g", (gdouble)v[i]),
		                                              YAML_SCALAR_STYLE_PLAIN);

		yaml_sequence_add_element (yaml_node_get_sequence (node), e);
	}
	return node;
}

/* reads @n numbers of a sequence node */
static gboolean
read_vec (YamlNode *node,
          gfloat   *out,
          guint     n)
{
	YamlSequence *seq;
	guint         i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return FALSE;
	seq = yaml_node_get_sequence (node);
	if (yaml_sequence_get_length (seq) != n)
		return FALSE;
	for (i = 0; i < n; i++)
	{
		YamlNode    *e = yaml_sequence_get_element (seq, i);
		const gchar *s;
		gchar       *end = NULL;

		if (e == NULL || yaml_node_get_node_type (e) != YAML_NODE_SCALAR)
			return FALSE;
		s = yaml_node_get_scalar (e);
		if (s == NULL)
			return FALSE;
		out[i] = (gfloat)g_ascii_strtod (s, &end);
		if (end == s || *end != '\0')
			return FALSE;
	}
	return TRUE;
}

gboolean
lrg_probe_grid_save (LrgProbeGrid  *self,
                     const gchar   *path,
                     GError       **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      bounds = NULL;
	g_autoptr(YamlNode)      vmin = NULL;
	g_autoptr(YamlNode)      vmax = NULL;
	g_autoptr(YamlNode)      res = NULL;
	g_autoptr(YamlNode)      inten = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autofree gchar        *text = NULL;
	g_autofree gchar        *b64_sh = NULL;
	g_autofree gchar        *b64_valid = NULL;
	g_autofree guint32      *le = NULL;
	YamlMapping             *map;
	gfloat                   resf[3];
	gsize                    nf, i;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* coefficients as little-endian float32 bits */
	nf = (gsize)probe_count (self) * LRG_PROBE_GRID_SH_FLOATS;
	le = g_new (guint32, nf);
	for (i = 0; i < nf; i++)
	{
		guint32 bits;

		memcpy (&bits, &self->sh[i], sizeof bits);
		le[i] = GUINT32_TO_LE (bits);
	}
	b64_sh = g_base64_encode ((const guchar *)le, nf * sizeof (guint32));
	b64_valid = g_base64_encode (self->valid, probe_count (self));

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_PROBE_GRID_FORMAT_VERSION);
	bounds = yaml_node_new_mapping (NULL);
	vmin = vec_node (self->min, 3);
	vmax = vec_node (self->max, 3);
	yaml_mapping_set_member (yaml_node_get_mapping (bounds), "min", vmin);
	yaml_mapping_set_member (yaml_node_get_mapping (bounds), "max", vmax);
	yaml_mapping_set_member (map, "bounds", bounds);
	resf[0] = (gfloat)self->n[0];
	resf[1] = (gfloat)self->n[1];
	resf[2] = (gfloat)self->n[2];
	res = vec_node (resf, 3);
	yaml_mapping_set_member (map, "resolution", res);
	{
		gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

		inten = yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.9g", (gdouble)self->intensity),
		                              YAML_SCALAR_STYLE_PLAIN);
		yaml_mapping_set_member (map, "intensity", inten);
	}
	yaml_mapping_set_string_member (map, "encoding", "base64-f32le");
	yaml_mapping_set_string_member (map, "sh", b64_sh);
	yaml_mapping_set_string_member (map, "valid", b64_valid);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	text = yaml_generator_to_data (gen, NULL, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, NULL))
	{
		g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_IO,
		             "cannot write probe grid '%s'", path);
		return FALSE;
	}
	g_free (self->path);
	self->path = g_strdup (path);
	return TRUE;
}

LrgProbeGrid *
lrg_probe_grid_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autoptr(LrgProbeGrid) self = NULL;
	g_autoptr(YamlParser)   parser = NULL;
	g_autoptr(GError)       local = NULL;
	g_autofree guchar      *sh = NULL;
	g_autofree guchar      *valid = NULL;
	YamlNode               *root;
	YamlMapping            *map;
	YamlNode               *bnode;
	const gchar            *s;
	gfloat                  bmin[3], bmax[3], res[3], one;
	gsize                   sh_len = 0, valid_len = 0, nf, i;
	guint                   count;

	g_return_val_if_fail (path != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, &local))
	{
		g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_IO,
		             "cannot read probe grid '%s': %s", path, local->message);
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		goto bad;
	map = yaml_node_get_mapping (root);

	/* version gate */
	if (yaml_mapping_has_member (map, "version")
	    && yaml_mapping_get_int_member (map, "version") > LRG_PROBE_GRID_FORMAT_VERSION)
	{
		g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_VERSION,
		             "probe grid '%s' is version %" G_GINT64_FORMAT ", this build reads %d", path,
		             yaml_mapping_get_int_member (map, "version"), LRG_PROBE_GRID_FORMAT_VERSION);
		return NULL;
	}

	/* layout */
	bnode = yaml_mapping_get_member (map, "bounds");
	if (bnode == NULL || yaml_node_get_node_type (bnode) != YAML_NODE_MAPPING
	    || !read_vec (yaml_mapping_get_member (yaml_node_get_mapping (bnode), "min"), bmin, 3)
	    || !read_vec (yaml_mapping_get_member (yaml_node_get_mapping (bnode), "max"), bmax, 3)
	    || !read_vec (yaml_mapping_get_member (map, "resolution"), res, 3)
	    || res[0] < 1.0f || res[1] < 1.0f || res[2] < 1.0f
	    || res[0] > LRG_PROBE_GRID_MAX_RESOLUTION || res[1] > LRG_PROBE_GRID_MAX_RESOLUTION
	    || res[2] > LRG_PROBE_GRID_MAX_RESOLUTION)
		goto bad;
	s = yaml_mapping_get_string_member (map, "encoding");
	if (s != NULL && g_strcmp0 (s, "base64-f32le") != 0)
	{
		g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT,
		             "probe grid '%s': unknown encoding '%s'", path, s);
		return NULL;
	}
	self = lrg_probe_grid_new_with_bounds (bmin, bmax, (guint)res[0], (guint)res[1], (guint)res[2]);
	{
		YamlNode *inode = yaml_mapping_get_member (map, "intensity");

		if (inode != NULL && yaml_node_get_node_type (inode) == YAML_NODE_SCALAR
		    && yaml_node_get_scalar (inode) != NULL)
		{
			one = (gfloat)g_ascii_strtod (yaml_node_get_scalar (inode), NULL);
			self->intensity = MAX (one, 0.0f);
		}
	}

	/* data: sizes must match the resolution exactly */
	count = probe_count (self);
	nf = (gsize)count * LRG_PROBE_GRID_SH_FLOATS;
	s = yaml_mapping_get_string_member (map, "sh");
	if (s != NULL && *s != '\0')
	{
		sh = g_base64_decode (s, &sh_len);
		if (sh_len != nf * sizeof (guint32))
		{
			g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT,
			             "probe grid '%s': 'sh' holds %" G_GSIZE_FORMAT " bytes, %" G_GSIZE_FORMAT " expected",
			             path, sh_len, nf * sizeof (guint32));
			return NULL;
		}
		for (i = 0; i < nf; i++)
		{
			guint32 bits;

			memcpy (&bits, sh + i * sizeof (guint32), sizeof bits);
			bits = GUINT32_FROM_LE (bits);
			memcpy (&self->sh[i], &bits, sizeof bits);
		}
	}
	s = yaml_mapping_get_string_member (map, "valid");
	if (s != NULL && *s != '\0')
	{
		valid = g_base64_decode (s, &valid_len);
		if (valid_len != count)
		{
			g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT,
			             "probe grid '%s': 'valid' holds %" G_GSIZE_FORMAT " flags, %u expected",
			             path, valid_len, count);
			return NULL;
		}
		for (i = 0; i < count; i++)
			self->valid[i] = valid[i] != 0 ? 1 : 0;
	}
	self->path = g_strdup (path);
	mark_changed (self);
	return g_steal_pointer (&self);

bad:
	g_set_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT,
	             "probe grid '%s' needs a mapping with bounds {min, max} and a 3-number resolution "
	             "(1 .. %d each)", path, LRG_PROBE_GRID_MAX_RESOLUTION);
	return NULL;
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

gboolean
lrg_probe_grid_upload (LrgProbeGrid  *self,
                       GError       **error)
{
	g_autofree gfloat *texels = NULL;
	guint              nx, ny, nz, x, y, z, g, k;

	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), FALSE);

	if (!gl3d_load ())
	{
		g_set_error_literal (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_GPU,
		                     "uploading a probe grid needs a GL 3.3 context");
		return FALSE;
	}
	if (self->texture != 0 && !self->gpu_dirty)
		return TRUE;

	resolve (self);
	nx = self->n[0];
	ny = self->n[1];
	nz = self->n[2];

	/* texel (x, y, g nz + z) = floats 4g .. 4g + 3 of the padded probe */
	texels = g_new0 (gfloat, (gsize)nx * ny * nz * GROUPS * 4);
	for (z = 0; z < nz; z++)
		for (y = 0; y < ny; y++)
			for (x = 0; x < nx; x++)
			{
				guint         idx = x + nx * (y + ny * z);
				const gfloat *src = self->resolved + (gsize)idx * LRG_PROBE_GRID_SH_FLOATS;

				for (g = 0; g < GROUPS; g++)
				{
					gsize dst = (((gsize)(g * nz + z) * ny + y) * nx + x) * 4;

					for (k = 0; k < 4; k++)
					{
						guint f = g * 4 + k;

						texels[dst + k] = f < LRG_PROBE_GRID_SH_FLOATS ? src[f]
						                  : (self->valid[idx] ? 1.0f : 0.0f);
					}
				}
			}

	if (self->texture == 0)
		gl3d.gen_textures (1, &self->texture);
	if (self->texture == 0)
	{
		g_set_error_literal (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_GPU,
		                     "glGenTextures failed");
		return FALSE;
	}
	rlDrawRenderBatchActive ();
	gl3d.active_texture (GL_TEXTURE0_);
	gl3d.bind_texture (GL_TEXTURE_3D_, self->texture);
	gl3d.pixel_storei (GL_UNPACK_ALIGNMENT_, 4);
	gl3d.tex_image_3d (GL_TEXTURE_3D_, 0, GL_RGBA16F_, (gint)nx, (gint)ny, (gint)(nz * GROUPS), 0,
	                   GL_RGBA_, GL_FLOAT_, texels);
	gl3d.tex_parameteri (GL_TEXTURE_3D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
	gl3d.tex_parameteri (GL_TEXTURE_3D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
	gl3d.tex_parameteri (GL_TEXTURE_3D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
	gl3d.tex_parameteri (GL_TEXTURE_3D_, GL_TEXTURE_WRAP_T_, GL_CLAMP_TO_EDGE_);
	gl3d.tex_parameteri (GL_TEXTURE_3D_, GL_TEXTURE_WRAP_R_, GL_CLAMP_TO_EDGE_);
	gl3d.bind_texture (GL_TEXTURE_3D_, 0);
	self->gpu_dirty = FALSE;
	return TRUE;
}

void
lrg_probe_grid_unload_gpu (LrgProbeGrid *self)
{
	g_return_if_fail (LRG_IS_PROBE_GRID (self));

	if (self->texture != 0 && gl3d_load ())
		gl3d.delete_textures (1, &self->texture);
	self->texture = 0;
	self->gpu_dirty = TRUE;
}

guint
lrg_probe_grid_get_texture (LrgProbeGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROBE_GRID (self), 0);
	return self->gpu_dirty ? 0 : self->texture;
}
