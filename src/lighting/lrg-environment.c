/* lrg-environment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEnvironment: sky source, image-based lighting bake (radiance cube,
 * L2 irradiance harmonics, GGX-prefiltered specular mips, BRDF table),
 * ambient / fog / volumetric settings, reflection-probe selection, the
 * lit-shader binder and the `environment:` YAML block.
 *
 * GPU work uses GL 3.3 through rlgl only: every derived map is produced
 * by drawing a full-screen quad into one face of one mip of a cubemap
 * attached to a framebuffer (no compute, no glGenerateMipmap).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-environment.h"
#include "lrg-environment-glsl-private.h"
#include "../core/lrg-asset-manager.h"
#include "../core/lrg-math3d.h"
#include "../graphics/lrg-light-set-private.h"

#define ENV_PI           (3.14159265358979323846)
#define ENV_CHAIN_MAX    (8)
#define ENV_PREFILTER_SAMPLES (64)
#define ENV_BRDF_SAMPLES (256)

/* ==========================================================================
 * Types
 * ========================================================================== */

/* One YAML member the environment does not know, kept for the writer.
 * @group is NULL for the top level of the block, else the sub-block. */
typedef struct
{
	gchar    *group;
	gchar    *key;
	YamlNode *node;
} EnvExtra;

static void
env_extra_free (gpointer data)
{
	EnvExtra *e = data;

	g_free (e->group);
	g_free (e->key);
	if (e->node != NULL)
		yaml_node_unref (e->node);
	g_free (e);
}

struct _LrgEnvironment
{
	GObject parent_instance;

	/* sky */
	LrgEnvironmentSkySource source;
	gfloat       sky_color[3];
	gfloat       zenith[3];
	gfloat       horizon[3];
	gfloat       ground[3];
	LrgSkyDome  *dome;
	LrgHdrImage *equirect;
	LrgHdrImage *faces[6];
	gchar       *sky_path;
	gchar       *face_paths[6];

	/* intensity */
	gfloat       exposure;
	gfloat       rotation;
	gfloat       diffuse_intensity;
	gfloat       specular_intensity;
	gboolean     ibl_enabled;
	guint        resolution;

	/* ambient, fog, volumetrics, post */
	gfloat       ambient[3];
	gfloat       ambient_intensity;
	gboolean     fog_enabled;
	LrgFogType   fog_type;
	gfloat       fog_rgba[4];
	gfloat       fog_density;
	gfloat       fog_start;
	gfloat       fog_end;
	gfloat       fog_base;
	gfloat       fog_falloff;
	gboolean     vol_enabled;
	gfloat       vol_density;
	gfloat       vol_anisotropy;
	guint        vol_steps;
	gfloat       vol_max_distance;
	gfloat       vol_intensity;
	gchar       *post_stack;

	/* probes */
	GPtrArray   *probes;	/* LrgReflectionProbe* (owned) */
	gfloat       origin[3];
	gfloat       reference[3];

	/* irradiance */
	gfloat       sh[LRG_ENVIRONMENT_SH_FLOATS];

	/* GPU */
	guint        radiance_id;
	guint        specular_id;
	guint        brdf_id;
	guint        baked_size;
	guint        specular_mips;
	gboolean     dirty;
	Shader       sky_shader;
	gboolean     sky_shader_ok;
	gint         sky_loc_eye;
	gint         sky_loc_map;
	gint         sky_loc_params;

	/* YAML members we do not know */
	GPtrArray   *extras;	/* EnvExtra* */
};

G_DEFINE_FINAL_TYPE (LrgEnvironment, lrg_environment, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-environment-error-quark, lrg_environment_error)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

GType
lrg_environment_sky_source_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ENVIRONMENT_SKY_COLOR, "LRG_ENVIRONMENT_SKY_COLOR", "color" },
			{ LRG_ENVIRONMENT_SKY_GRADIENT, "LRG_ENVIRONMENT_SKY_GRADIENT", "gradient" },
			{ LRG_ENVIRONMENT_SKY_DOME, "LRG_ENVIRONMENT_SKY_DOME", "sky-dome" },
			{ LRG_ENVIRONMENT_SKY_CUBEMAP, "LRG_ENVIRONMENT_SKY_CUBEMAP", "cubemap" },
			{ LRG_ENVIRONMENT_SKY_EQUIRECT, "LRG_ENVIRONMENT_SKY_EQUIRECT", "equirect" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEnvironmentSkySource"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_environment_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ENVIRONMENT_ERROR_FAILED, "LRG_ENVIRONMENT_ERROR_FAILED", "failed" },
			{ LRG_ENVIRONMENT_ERROR_IO, "LRG_ENVIRONMENT_ERROR_IO", "io" },
			{ LRG_ENVIRONMENT_ERROR_PARSE, "LRG_ENVIRONMENT_ERROR_PARSE", "parse" },
			{ LRG_ENVIRONMENT_ERROR_SOURCE, "LRG_ENVIRONMENT_ERROR_SOURCE", "source" },
			{ LRG_ENVIRONMENT_ERROR_GPU, "LRG_ENVIRONMENT_ERROR_GPU", "gpu" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEnvironmentError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static void
vec3_set (gfloat *dst, gfloat x, gfloat y, gfloat z)
{
	dst[0] = x;
	dst[1] = y;
	dst[2] = z;
}

static void
vec3_copy (gfloat *dst, const gfloat *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

/* sRGB (gamma 2.2, the light set's model) to linear */
static gfloat
to_linear (gfloat c)
{
	return c > 0.0f ? powf (c, 2.2f) : 0.0f;
}

static gfloat
exposure_factor (LrgEnvironment *self)
{
	return exp2f (self->exposure);
}

/* the sky rotation actually applied (a dome is drawn unrotated) */
static gfloat
effective_rotation (LrgEnvironment *self)
{
	return self->source == LRG_ENVIRONMENT_SKY_DOME ? 0.0f : self->rotation;
}

/* lookup = R_y (-rotation) d */
static void
rotate_lookup (gfloat degrees, const gfloat *d, gfloat *out)
{
	gfloat a = degrees * (gfloat)(ENV_PI / 180.0);
	gfloat c = cosf (a);
	gfloat s = sinf (a);
	gfloat x = d[0];
	gfloat z = d[2];

	out[0] = c * x - s * z;
	out[1] = d[1];
	out[2] = s * x + c * z;
}

/* Converts a column-major float[16] into a raylib Matrix by field name. */
static Matrix
matrix_from_floats (const gfloat *m)
{
	Matrix r;

	r.m0 = m[0];   r.m1 = m[1];   r.m2 = m[2];   r.m3 = m[3];
	r.m4 = m[4];   r.m5 = m[5];   r.m6 = m[6];   r.m7 = m[7];
	r.m8 = m[8];   r.m9 = m[9];   r.m10 = m[10]; r.m11 = m[11];
	r.m12 = m[12]; r.m13 = m[13]; r.m14 = m[14]; r.m15 = m[15];
	return r;
}

/* IEEE half from float (round to nearest, flush tiny values to zero) */
static guint16
float_to_half (gfloat f)
{
	union { gfloat f; guint32 u; } v;
	guint32 sign;
	gint32  exp;
	guint32 mant;

	v.f = f;
	sign = (v.u >> 16) & 0x8000u;
	exp = (gint32)((v.u >> 23) & 0xff) - 127 + 15;
	mant = v.u & 0x7fffffu;
	if (((v.u >> 23) & 0xff) == 0xff)
		return (guint16)(sign | 0x7c00u | (mant != 0 ? 0x200u : 0u));
	if (exp <= 0)
		return (guint16)sign;
	if (exp >= 31)
		return (guint16)(sign | 0x7c00u);
	mant += 0x1000u;	/* round */
	if (mant & 0x800000u)
	{
		mant = 0;
		exp++;
		if (exp >= 31)
			return (guint16)(sign | 0x7c00u);
	}
	return (guint16)(sign | ((guint32)exp << 10) | (mant >> 13));
}

static void
emit_changed (LrgEnvironment *self,
              gboolean        maps)
{
	if (maps)
		self->dirty = TRUE;
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
release_gpu_internal (LrgEnvironment *self)
{
	gboolean ready = IsWindowReady ();

	if (ready)
	{
		lrg_env_gpu_unload (self->radiance_id);
		lrg_env_gpu_unload (self->specular_id);
		lrg_env_gpu_unload (self->brdf_id);
		if (self->sky_shader_ok)
			UnloadShader (self->sky_shader);
	}
	self->radiance_id = 0;
	self->specular_id = 0;
	self->brdf_id = 0;
	self->specular_mips = 0;
	self->baked_size = 0;
	self->sky_shader_ok = FALSE;
	memset (&self->sky_shader, 0, sizeof (self->sky_shader));
}

static void
lrg_environment_finalize (GObject *object)
{
	LrgEnvironment *self = LRG_ENVIRONMENT (object);
	guint i;

	release_gpu_internal (self);
	g_clear_object (&self->dome);
	g_clear_object (&self->equirect);
	for (i = 0; i < 6; i++)
	{
		g_clear_object (&self->faces[i]);
		g_clear_pointer (&self->face_paths[i], g_free);
	}
	g_clear_pointer (&self->sky_path, g_free);
	g_clear_pointer (&self->post_stack, g_free);
	g_clear_pointer (&self->probes, g_ptr_array_unref);
	g_clear_pointer (&self->extras, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_environment_parent_class)->finalize (object);
}

static void
lrg_environment_class_init (LrgEnvironmentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_environment_finalize;

	/**
	 * LrgEnvironment::changed:
	 * @self: the environment
	 *
	 * Emitted after any setting changed (sky, intensity, ambient, fog,
	 * volumetric, post stack, probes).  lrg_environment_needs_bake()
	 * tells whether the change requires a new bake.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/* the defaults of lrg_environment_new() (also what a YAML block resets to) */
static void
set_defaults (LrgEnvironment *self)
{
	guint i;

	self->source = LRG_ENVIRONMENT_SKY_GRADIENT;
	vec3_set (self->sky_color, 0.5f, 0.5f, 0.5f);
	vec3_set (self->zenith, 0.35f, 0.5f, 0.75f);
	vec3_set (self->horizon, 0.7f, 0.72f, 0.75f);
	vec3_set (self->ground, 0.3f, 0.28f, 0.25f);
	g_clear_object (&self->dome);
	g_clear_object (&self->equirect);
	for (i = 0; i < 6; i++)
	{
		g_clear_object (&self->faces[i]);
		g_clear_pointer (&self->face_paths[i], g_free);
	}
	g_clear_pointer (&self->sky_path, g_free);
	self->exposure = 0.0f;
	self->rotation = 0.0f;
	self->diffuse_intensity = 1.0f;
	self->specular_intensity = 1.0f;
	self->ibl_enabled = TRUE;
	self->resolution = LRG_ENVIRONMENT_DEFAULT_RESOLUTION;
	vec3_set (self->ambient, 0.2f, 0.2f, 0.2f);
	self->ambient_intensity = 1.0f;
	self->fog_enabled = FALSE;
	self->fog_type = LRG_FOG_TYPE_EXPONENTIAL;
	self->fog_rgba[0] = 0.7f;
	self->fog_rgba[1] = 0.75f;
	self->fog_rgba[2] = 0.8f;
	self->fog_rgba[3] = 1.0f;
	self->fog_density = 0.02f;
	self->fog_start = 10.0f;
	self->fog_end = 100.0f;
	self->fog_base = 0.0f;
	self->fog_falloff = 0.1f;
	self->vol_enabled = FALSE;
	self->vol_density = 0.02f;
	self->vol_anisotropy = 0.6f;
	self->vol_steps = 32;
	self->vol_max_distance = 100.0f;
	self->vol_intensity = 1.0f;
	g_clear_pointer (&self->post_stack, g_free);
	self->dirty = TRUE;
}

static void
lrg_environment_init (LrgEnvironment *self)
{
	self->probes = g_ptr_array_new_with_free_func (g_object_unref);
	self->extras = g_ptr_array_new_with_free_func (env_extra_free);
	set_defaults (self);
}

LrgEnvironment *
lrg_environment_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT, NULL);
}

/* ==========================================================================
 * Sky source
 * ========================================================================== */

void
lrg_environment_set_sky_source (LrgEnvironment          *self,
                                LrgEnvironmentSkySource  source)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (source <= LRG_ENVIRONMENT_SKY_EQUIRECT);

	if (self->source == source)
		return;
	self->source = source;
	emit_changed (self, TRUE);
}

LrgEnvironmentSkySource
lrg_environment_get_sky_source (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), LRG_ENVIRONMENT_SKY_GRADIENT);
	return self->source;
}

void
lrg_environment_set_sky_color (LrgEnvironment *self,
                               const gfloat   *rgb)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (rgb != NULL);

	vec3_copy (self->sky_color, rgb);
	emit_changed (self, TRUE);
}

void
lrg_environment_get_sky_color (LrgEnvironment *self,
                               gfloat         *out_rgb)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (out_rgb != NULL);
	vec3_copy (out_rgb, self->sky_color);
}

void
lrg_environment_set_gradient (LrgEnvironment *self,
                              const gfloat   *zenith,
                              const gfloat   *horizon,
                              const gfloat   *ground)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (zenith != NULL && horizon != NULL && ground != NULL);

	vec3_copy (self->zenith, zenith);
	vec3_copy (self->horizon, horizon);
	vec3_copy (self->ground, ground);
	emit_changed (self, TRUE);
}

void
lrg_environment_get_gradient (LrgEnvironment *self,
                              gfloat         *out_zenith,
                              gfloat         *out_horizon,
                              gfloat         *out_ground)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (out_zenith != NULL)
		vec3_copy (out_zenith, self->zenith);
	if (out_horizon != NULL)
		vec3_copy (out_horizon, self->horizon);
	if (out_ground != NULL)
		vec3_copy (out_ground, self->ground);
}

void
lrg_environment_set_sky_dome (LrgEnvironment *self,
                              LrgSkyDome     *dome)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (dome == NULL || LRG_IS_SKY_DOME (dome));

	if (g_set_object (&self->dome, dome))
		emit_changed (self, TRUE);
}

LrgSkyDome *
lrg_environment_get_sky_dome (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	return self->dome;
}

void
lrg_environment_set_equirect (LrgEnvironment *self,
                              LrgHdrImage    *image)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (image == NULL || LRG_IS_HDR_IMAGE (image));

	if (g_set_object (&self->equirect, image))
		emit_changed (self, TRUE);
}

LrgHdrImage *
lrg_environment_get_equirect (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	return self->equirect;
}

void
lrg_environment_set_cubemap_face (LrgEnvironment *self,
                                  guint           face,
                                  LrgHdrImage    *image)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (face < 6);
	g_return_if_fail (image == NULL || LRG_IS_HDR_IMAGE (image));

	if (g_set_object (&self->faces[face], image))
		emit_changed (self, TRUE);
}

LrgHdrImage *
lrg_environment_get_cubemap_face (LrgEnvironment *self,
                                  guint           face)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	g_return_val_if_fail (face < 6, NULL);
	return self->faces[face];
}

/* Copies a @size square at cell (@cx, @cy) of @src into a new image,
 * rotated half a turn when @flip (the -Z face of vertical crosses). */
static LrgHdrImage *
cut_face (LrgHdrImage *src,
          guint        size,
          guint        cx,
          guint        cy,
          gboolean     flip)
{
	LrgHdrImage *face = lrg_hdr_image_new (size, size);
	guint        x;
	guint        y;
	gfloat       rgb[3];

	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
		{
			lrg_hdr_image_get_pixel (src, cx * size + x, cy * size + y, rgb);
			if (flip)
				lrg_hdr_image_set_pixel (face, size - 1 - x, size - 1 - y, rgb);
			else
				lrg_hdr_image_set_pixel (face, x, y, rgb);
		}
	}
	return face;
}

gboolean
lrg_environment_set_cubemap_cross (LrgEnvironment  *self,
                                   LrgHdrImage     *cross,
                                   GError         **error)
{
	guint w;
	guint h;
	guint size;
	guint i;
	LrgHdrImage *faces[6];

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (cross), FALSE);

	w = lrg_hdr_image_get_width (cross);
	h = lrg_hdr_image_get_height (cross);
	if (w * 3 == h * 4 && w % 4 == 0)
	{
		/* horizontal:   . +Y  .  .
		 *              -X +Z +X -Z
		 *               . -Y  .  .  */
		size = w / 4;
		faces[0] = cut_face (cross, size, 2, 1, FALSE);
		faces[1] = cut_face (cross, size, 0, 1, FALSE);
		faces[2] = cut_face (cross, size, 1, 0, FALSE);
		faces[3] = cut_face (cross, size, 1, 2, FALSE);
		faces[4] = cut_face (cross, size, 1, 1, FALSE);
		faces[5] = cut_face (cross, size, 3, 1, FALSE);
	}
	else if (w * 4 == h * 3 && w % 3 == 0)
	{
		/* vertical:     . +Y  .
		 *              -X +Z +X
		 *               . -Y  .
		 *               . -Z  .   (upside down) */
		size = w / 3;
		faces[0] = cut_face (cross, size, 2, 1, FALSE);
		faces[1] = cut_face (cross, size, 0, 1, FALSE);
		faces[2] = cut_face (cross, size, 1, 0, FALSE);
		faces[3] = cut_face (cross, size, 1, 2, FALSE);
		faces[4] = cut_face (cross, size, 1, 1, FALSE);
		faces[5] = cut_face (cross, size, 1, 3, TRUE);
	}
	else
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
		             "A %ux%u image is not a 4x3 or 3x4 cubemap cross", w, h);
		return FALSE;
	}

	for (i = 0; i < 6; i++)
	{
		g_set_object (&self->faces[i], faces[i]);
		g_object_unref (faces[i]);
	}
	emit_changed (self, TRUE);
	return TRUE;
}

void
lrg_environment_set_sky_path (LrgEnvironment *self,
                              const gchar    *path)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (g_strcmp0 (self->sky_path, path) == 0)
		return;
	g_free (self->sky_path);
	self->sky_path = (path != NULL && *path != '\0') ? g_strdup (path) : NULL;
	emit_changed (self, TRUE);
}

const gchar *
lrg_environment_get_sky_path (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	return self->sky_path;
}

void
lrg_environment_set_sky_face_path (LrgEnvironment *self,
                                   guint           face,
                                   const gchar    *path)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (face < 6);

	if (g_strcmp0 (self->face_paths[face], path) == 0)
		return;
	g_free (self->face_paths[face]);
	self->face_paths[face] = (path != NULL && *path != '\0') ? g_strdup (path) : NULL;
	emit_changed (self, TRUE);
}

const gchar *
lrg_environment_get_sky_face_path (LrgEnvironment *self,
                                   guint           face)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	g_return_val_if_fail (face < 6, NULL);
	return self->face_paths[face];
}

/* Resolves an authored reference (path or guid:) to a file path. */
static gchar *
resolve_reference (const gchar      *ref,
                   LrgAssetManager  *manager,
                   const gchar      *base_dir,
                   GError          **error)
{
	if (g_str_has_prefix (ref, LRG_ASSET_GUID_PREFIX))
	{
		gchar *path = manager != NULL ? lrg_asset_manager_resolve_guid (manager, ref) : NULL;

		if (path == NULL)
			g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
			             "Cannot resolve %s%s", ref,
			             manager == NULL ? " (no asset manager)" : "");
		return path;
	}
	if (g_path_is_absolute (ref) || base_dir == NULL)
		return g_strdup (ref);
	return g_build_filename (base_dir, ref, NULL);
}

/* Loads one sky image as linear floats: .hdr through LrgHdrImage, other
 * formats through raylib (sRGB decoded). */
static LrgHdrImage *
load_sky_image (const gchar  *path,
                GError      **error)
{
	Image        img;
	LrgHdrImage *out;
	gfloat      *data;
	const gfloat *src;
	gsize        n;
	gsize        i;

	if (g_str_has_suffix (path, ".hdr") || g_str_has_suffix (path, ".HDR"))
	{
		g_autoptr(GError) hdr_error = NULL;

		out = lrg_hdr_image_new_from_file (path, &hdr_error);
		if (out == NULL)
			g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
			             "%s", hdr_error->message);
		return out;
	}

	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
		             "Sky image %s does not exist", path);
		return NULL;
	}
	img = LoadImage (path);
	if (img.data == NULL || img.width <= 0 || img.height <= 0)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
		             "Cannot decode sky image %s", path);
		return NULL;
	}
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R32G32B32);
	out = lrg_hdr_image_new ((guint)img.width, (guint)img.height);
	data = lrg_hdr_image_get_data (out, &n);
	src = (const gfloat *)img.data;
	for (i = 0; i < n; i++)
		data[i] = to_linear (src[i]);
	UnloadImage (img);
	return out;
}

gboolean
lrg_environment_load_sources (LrgEnvironment   *self,
                              LrgAssetManager  *manager,
                              const gchar      *base_dir,
                              GError          **error)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);

	if (self->source == LRG_ENVIRONMENT_SKY_EQUIRECT)
	{
		g_autofree gchar *path = NULL;
		LrgHdrImage      *img;

		if (self->sky_path == NULL)
		{
			g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
			             "The equirect sky has no path");
			return FALSE;
		}
		path = resolve_reference (self->sky_path, manager, base_dir, error);
		if (path == NULL)
			return FALSE;
		img = load_sky_image (path, error);
		if (img == NULL)
			return FALSE;
		g_set_object (&self->equirect, img);
		g_object_unref (img);
		emit_changed (self, TRUE);
		return TRUE;
	}

	if (self->source == LRG_ENVIRONMENT_SKY_CUBEMAP)
	{
		LrgHdrImage *loaded[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
		guint        i;
		guint        size = 0;

		/* a cross in the sky path, else six face paths */
		if (self->sky_path != NULL)
		{
			g_autofree gchar *path = resolve_reference (self->sky_path, manager, base_dir, error);
			g_autoptr(LrgHdrImage) cross = NULL;

			if (path == NULL)
				return FALSE;
			cross = load_sky_image (path, error);
			if (cross == NULL)
				return FALSE;
			return lrg_environment_set_cubemap_cross (self, cross, error);
		}
		for (i = 0; i < 6; i++)
		{
			g_autofree gchar *path = NULL;

			if (self->face_paths[i] == NULL)
			{
				g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
				             "Cubemap face %u has no path", i);
				goto fail;
			}
			path = resolve_reference (self->face_paths[i], manager, base_dir, error);
			if (path == NULL)
				goto fail;
			loaded[i] = load_sky_image (path, error);
			if (loaded[i] == NULL)
				goto fail;
			if (lrg_hdr_image_get_width (loaded[i]) != lrg_hdr_image_get_height (loaded[i]) ||
			    (size != 0 && lrg_hdr_image_get_width (loaded[i]) != size))
			{
				g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE,
				             "Cubemap face %s is not square or differs in size", path);
				goto fail;
			}
			size = lrg_hdr_image_get_width (loaded[i]);
		}
		for (i = 0; i < 6; i++)
		{
			g_set_object (&self->faces[i], loaded[i]);
			g_object_unref (loaded[i]);
		}
		emit_changed (self, TRUE);
		return TRUE;

fail:
		for (i = 0; i < 6; i++)
			g_clear_object (&loaded[i]);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Intensity and orientation
 * ========================================================================== */

void
lrg_environment_set_exposure (LrgEnvironment *self,
                              gfloat          ev)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	self->exposure = CLAMP (ev, -32.0f, 32.0f);
	emit_changed (self, FALSE);
}

gfloat
lrg_environment_get_exposure (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0.0f);
	return self->exposure;
}

void
lrg_environment_set_rotation (LrgEnvironment *self,
                              gfloat          degrees)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	self->rotation = isfinite (degrees) ? fmodf (degrees, 360.0f) : 0.0f;
	emit_changed (self, FALSE);
}

gfloat
lrg_environment_get_rotation (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0.0f);
	return self->rotation;
}

void
lrg_environment_set_diffuse_intensity (LrgEnvironment *self,
                                       gfloat          intensity)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	self->diffuse_intensity = MAX (intensity, 0.0f);
	emit_changed (self, FALSE);
}

gfloat
lrg_environment_get_diffuse_intensity (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 1.0f);
	return self->diffuse_intensity;
}

void
lrg_environment_set_specular_intensity (LrgEnvironment *self,
                                        gfloat          intensity)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	self->specular_intensity = MAX (intensity, 0.0f);
	emit_changed (self, FALSE);
}

gfloat
lrg_environment_get_specular_intensity (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 1.0f);
	return self->specular_intensity;
}

void
lrg_environment_set_ibl_enabled (LrgEnvironment *self,
                                 gboolean        enabled)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	self->ibl_enabled = enabled != FALSE;
	emit_changed (self, FALSE);
}

gboolean
lrg_environment_get_ibl_enabled (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	return self->ibl_enabled;
}

/* power of two at or below @size within 16 .. @max */
static guint
pot_floor (guint size,
           guint max)
{
	guint p = 16;

	size = CLAMP (size, 16u, max);
	while (p * 2 <= size)
		p *= 2;
	return p;
}

void
lrg_environment_set_resolution (LrgEnvironment *self,
                                guint           size)
{
	guint p;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	p = pot_floor (size, LRG_ENVIRONMENT_MAX_RESOLUTION);
	if (p == self->resolution)
		return;
	self->resolution = p;
	emit_changed (self, TRUE);
}

guint
lrg_environment_get_resolution (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), LRG_ENVIRONMENT_DEFAULT_RESOLUTION);
	return self->resolution;
}

/* ==========================================================================
 * Ambient, fog, volumetrics, post stack
 * ========================================================================== */

void
lrg_environment_set_ambient (LrgEnvironment *self,
                             const gfloat   *rgb,
                             gfloat          intensity)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (rgb != NULL);

	vec3_copy (self->ambient, rgb);
	self->ambient_intensity = MAX (intensity, 0.0f);
	emit_changed (self, FALSE);
}

void
lrg_environment_get_ambient (LrgEnvironment *self,
                             gfloat         *out_rgb,
                             gfloat         *out_intensity)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (out_rgb != NULL)
		vec3_copy (out_rgb, self->ambient);
	if (out_intensity != NULL)
		*out_intensity = self->ambient_intensity;
}

void
lrg_environment_set_fog (LrgEnvironment *self,
                         gboolean        enabled,
                         LrgFogType      type,
                         const gfloat   *rgba,
                         gfloat          density,
                         gfloat          start,
                         gfloat          end)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (rgba != NULL);

	self->fog_enabled = enabled != FALSE;
	self->fog_type = type;
	memcpy (self->fog_rgba, rgba, sizeof (self->fog_rgba));
	self->fog_density = MAX (density, 0.0f);
	self->fog_start = start;
	self->fog_end = end;
	emit_changed (self, FALSE);
}

gboolean
lrg_environment_get_fog (LrgEnvironment *self,
                         LrgFogType     *out_type,
                         gfloat         *out_rgba,
                         gfloat         *out_density,
                         gfloat         *out_start,
                         gfloat         *out_end)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);

	if (out_type != NULL)
		*out_type = self->fog_type;
	if (out_rgba != NULL)
		memcpy (out_rgba, self->fog_rgba, sizeof (self->fog_rgba));
	if (out_density != NULL)
		*out_density = self->fog_density;
	if (out_start != NULL)
		*out_start = self->fog_start;
	if (out_end != NULL)
		*out_end = self->fog_end;
	return self->fog_enabled;
}

void
lrg_environment_set_fog_height (LrgEnvironment *self,
                                gfloat          base_height,
                                gfloat          falloff)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	self->fog_base = base_height;
	self->fog_falloff = MAX (falloff, 1e-4f);
	emit_changed (self, FALSE);
}

void
lrg_environment_get_fog_height (LrgEnvironment *self,
                                gfloat         *out_base_height,
                                gfloat         *out_falloff)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (out_base_height != NULL)
		*out_base_height = self->fog_base;
	if (out_falloff != NULL)
		*out_falloff = self->fog_falloff;
}

void
lrg_environment_set_volumetric (LrgEnvironment *self,
                                gboolean        enabled,
                                gfloat          density,
                                gfloat          anisotropy,
                                guint           steps,
                                gfloat          max_distance,
                                gfloat          intensity)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	self->vol_enabled = enabled != FALSE;
	self->vol_density = MAX (density, 0.0f);
	self->vol_anisotropy = CLAMP (anisotropy, -0.95f, 0.95f);
	self->vol_steps = CLAMP (steps, 4u, 128u);
	self->vol_max_distance = MAX (max_distance, 0.01f);
	self->vol_intensity = MAX (intensity, 0.0f);
	emit_changed (self, FALSE);
}

gboolean
lrg_environment_get_volumetric (LrgEnvironment *self,
                                gfloat         *out_density,
                                gfloat         *out_anisotropy,
                                guint          *out_steps,
                                gfloat         *out_max_distance,
                                gfloat         *out_intensity)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);

	if (out_density != NULL)
		*out_density = self->vol_density;
	if (out_anisotropy != NULL)
		*out_anisotropy = self->vol_anisotropy;
	if (out_steps != NULL)
		*out_steps = self->vol_steps;
	if (out_max_distance != NULL)
		*out_max_distance = self->vol_max_distance;
	if (out_intensity != NULL)
		*out_intensity = self->vol_intensity;
	return self->vol_enabled;
}

void
lrg_environment_set_post_stack (LrgEnvironment *self,
                                const gchar    *path)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (g_strcmp0 (self->post_stack, path) == 0)
		return;
	g_free (self->post_stack);
	self->post_stack = (path != NULL && *path != '\0') ? g_strdup (path) : NULL;
	emit_changed (self, FALSE);
}

const gchar *
lrg_environment_get_post_stack (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);
	return self->post_stack;
}

static guint8
color_byte (gfloat v)
{
	return (guint8)CLAMP (v * 255.0f + 0.5f, 0.0f, 255.0f);
}

void
lrg_environment_apply_to_light_set (LrgEnvironment *self,
                                    LrgLightSet    *light_set)
{
	GrlColor ambient;
	gfloat   k;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (LRG_IS_LIGHT_SET (light_set));

	/* the ambient colour is display-space like every light-set colour:
	 * scale it in linear space so intensity and exposure stay physical */
	k = powf (self->ambient_intensity * exposure_factor (self), 1.0f / 2.2f);
	ambient.r = color_byte (self->ambient[0] * k);
	ambient.g = color_byte (self->ambient[1] * k);
	ambient.b = color_byte (self->ambient[2] * k);
	ambient.a = 255;
	lrg_light_set_set_ambient (light_set, &ambient);

	if (self->fog_enabled)
	{
		GrlColor fog;

		fog.r = color_byte (self->fog_rgba[0]);
		fog.g = color_byte (self->fog_rgba[1]);
		fog.b = color_byte (self->fog_rgba[2]);
		fog.a = color_byte (self->fog_rgba[3]);
		lrg_light_set_set_fog (light_set, self->fog_type, &fog, self->fog_density,
		                       self->fog_start, self->fog_end);
		lrg_light_set_set_fog_height (light_set, self->fog_base, self->fog_falloff);
	}
	else
	{
		lrg_light_set_clear_fog (light_set);
	}
}

/* ==========================================================================
 * Probes
 * ========================================================================== */

static void
probe_changed_cb (LrgReflectionProbe *probe,
                  gpointer            user_data)
{
	emit_changed (LRG_ENVIRONMENT (user_data), FALSE);
}

void
lrg_environment_add_probe (LrgEnvironment     *self,
                           LrgReflectionProbe *probe)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (LRG_IS_REFLECTION_PROBE (probe));

	if (g_ptr_array_find (self->probes, probe, NULL))
		return;
	g_ptr_array_add (self->probes, g_object_ref (probe));
	g_signal_connect_object (probe, "changed", G_CALLBACK (probe_changed_cb), self, 0);
	emit_changed (self, FALSE);
}

gboolean
lrg_environment_remove_probe (LrgEnvironment     *self,
                              LrgReflectionProbe *probe)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);

	if (probe != NULL)
		g_signal_handlers_disconnect_by_func (probe, probe_changed_cb, self);
	if (!g_ptr_array_remove (self->probes, probe))
		return FALSE;
	emit_changed (self, FALSE);
	return TRUE;
}

void
lrg_environment_clear_probes (LrgEnvironment *self)
{
	guint i;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	if (self->probes->len == 0)
		return;
	for (i = 0; i < self->probes->len; i++)
		g_signal_handlers_disconnect_by_func (g_ptr_array_index (self->probes, i),
		                                      probe_changed_cb, self);
	g_ptr_array_set_size (self->probes, 0);
	emit_changed (self, FALSE);
}

guint
lrg_environment_get_n_probes (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	return self->probes->len;
}

LrgReflectionProbe *
lrg_environment_get_probe (LrgEnvironment *self,
                           guint           index)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);

	if (index >= self->probes->len)
		return NULL;
	return g_ptr_array_index (self->probes, index);
}

void
lrg_environment_set_reference_point (LrgEnvironment *self,
                                     gfloat          x,
                                     gfloat          y,
                                     gfloat          z)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	vec3_set (self->reference, x, y, z);
}

void
lrg_environment_set_origin (LrgEnvironment *self,
                            gfloat          x,
                            gfloat          y,
                            gfloat          z)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	vec3_set (self->origin, x, y, z);
}

guint
lrg_environment_select_probes (LrgEnvironment      *self,
                               const gfloat        *point,
                               LrgReflectionProbe **out_first,
                               LrgReflectionProbe **out_second)
{
	LrgReflectionProbe *best[2] = { NULL, NULL };
	gfloat              best_w[2] = { 0.0f, 0.0f };
	gfloat              best_d[2] = { 0.0f, 0.0f };
	guint               i;
	guint               n = 0;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	g_return_val_if_fail (point != NULL, 0);

	for (i = 0; i < self->probes->len; i++)
	{
		LrgReflectionProbe *p = g_ptr_array_index (self->probes, i);
		gfloat              pos[3];
		gfloat              w;
		gfloat              d;

		if (!lrg_reflection_probe_is_ready (p) || !lrg_reflection_probe_contains (p, point))
			continue;
		w = lrg_reflection_probe_get_influence (p, point);
		lrg_reflection_probe_get_position (p, pos);
		d = (pos[0] - point[0]) * (pos[0] - point[0]) + (pos[1] - point[1]) * (pos[1] - point[1]) +
		    (pos[2] - point[2]) * (pos[2] - point[2]);

		/* insert into the top two: larger influence first, then nearer */
		if (best[0] == NULL || w > best_w[0] || (w == best_w[0] && d < best_d[0]))
		{
			best[1] = best[0];
			best_w[1] = best_w[0];
			best_d[1] = best_d[0];
			best[0] = p;
			best_w[0] = w;
			best_d[0] = d;
		}
		else if (best[1] == NULL || w > best_w[1] || (w == best_w[1] && d < best_d[1]))
		{
			best[1] = p;
			best_w[1] = w;
			best_d[1] = d;
		}
	}
	if (best[0] != NULL)
		n++;
	if (best[1] != NULL)
		n++;
	if (out_first != NULL)
		*out_first = best[0];
	if (out_second != NULL)
		*out_second = best[1];
	return n;
}

/* ==========================================================================
 * CPU evaluation
 * ========================================================================== */

void
lrg_environment_cube_face_direction (guint   face,
                                     gfloat  s,
                                     gfloat  t,
                                     gfloat *out_direction)
{
	gfloat sc = s * 2.0f - 1.0f;
	gfloat tc = t * 2.0f - 1.0f;
	gfloat d[3];
	gfloat len;

	g_return_if_fail (out_direction != NULL);

	/* inverse of the OpenGL major-axis selection (spec table 3.21) */
	switch (face)
	{
	case 0:  vec3_set (d, 1.0f, -tc, -sc); break;
	case 1:  vec3_set (d, -1.0f, -tc, sc); break;
	case 2:  vec3_set (d, sc, 1.0f, tc); break;
	case 3:  vec3_set (d, sc, -1.0f, -tc); break;
	case 4:  vec3_set (d, sc, -tc, 1.0f); break;
	default: vec3_set (d, -sc, -tc, -1.0f); break;
	}
	len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	out_direction[0] = d[0] / len;
	out_direction[1] = d[1] / len;
	out_direction[2] = d[2] / len;
}

/* direction -> (face, s, t), the forward OpenGL cubemap lookup */
static guint
direction_to_face (const gfloat *d,
                   gfloat       *s,
                   gfloat       *t)
{
	gfloat ax = fabsf (d[0]);
	gfloat ay = fabsf (d[1]);
	gfloat az = fabsf (d[2]);
	gfloat ma;
	gfloat sc;
	gfloat tc;
	guint  face;

	if (ax >= ay && ax >= az)
	{
		face = d[0] > 0.0f ? 0 : 1;
		ma = ax;
		sc = d[0] > 0.0f ? -d[2] : d[2];
		tc = -d[1];
	}
	else if (ay >= az)
	{
		face = d[1] > 0.0f ? 2 : 3;
		ma = ay;
		sc = d[0];
		tc = d[1] > 0.0f ? d[2] : -d[2];
	}
	else
	{
		face = d[2] > 0.0f ? 4 : 5;
		ma = az;
		sc = d[2] > 0.0f ? d[0] : -d[0];
		tc = -d[1];
	}
	if (ma <= 0.0f)
		ma = 1.0f;
	*s = (sc / ma + 1.0f) * 0.5f;
	*t = (tc / ma + 1.0f) * 0.5f;
	return face;
}

/* bilinear lookup clamped to the face edges */
static void
sample_face_clamped (LrgHdrImage  *img,
                     gfloat        s,
                     gfloat        t,
                     gfloat       *out)
{
	guint  size = lrg_hdr_image_get_width (img);
	gfloat fx = CLAMP (s, 0.0f, 1.0f) * (gfloat)size - 0.5f;
	gfloat fy = CLAMP (t, 0.0f, 1.0f) * (gfloat)size - 0.5f;
	gint   x0 = (gint)floorf (fx);
	gint   y0 = (gint)floorf (fy);
	gfloat tx = fx - (gfloat)x0;
	gfloat ty = fy - (gfloat)y0;
	gfloat a[3];
	gfloat b[3];
	gfloat c[3];
	gfloat d[3];
	gint   x1;
	gint   y1;
	gint   k;

	x1 = CLAMP (x0 + 1, 0, (gint)size - 1);
	y1 = CLAMP (y0 + 1, 0, (gint)size - 1);
	x0 = CLAMP (x0, 0, (gint)size - 1);
	y0 = CLAMP (y0, 0, (gint)size - 1);
	lrg_hdr_image_get_pixel (img, (guint)x0, (guint)y0, a);
	lrg_hdr_image_get_pixel (img, (guint)x1, (guint)y0, b);
	lrg_hdr_image_get_pixel (img, (guint)x0, (guint)y1, c);
	lrg_hdr_image_get_pixel (img, (guint)x1, (guint)y1, d);
	for (k = 0; k < 3; k++)
	{
		gfloat top = a[k] + (b[k] - a[k]) * tx;
		gfloat bot = c[k] + (d[k] - c[k]) * tx;

		out[k] = top + (bot - top) * ty;
	}
}

static gboolean
faces_complete (LrgEnvironment *self)
{
	guint i;
	guint size;

	if (self->faces[0] == NULL)
		return FALSE;
	size = lrg_hdr_image_get_width (self->faces[0]);
	for (i = 0; i < 6; i++)
	{
		if (self->faces[i] == NULL ||
		    lrg_hdr_image_get_width (self->faces[i]) != size ||
		    lrg_hdr_image_get_height (self->faces[i]) != size)
			return FALSE;
	}
	return TRUE;
}

static void
gradient_eval (const gfloat *zenith,
               const gfloat *horizon,
               const gfloat *ground,
               const gfloat *d,
               gfloat       *out)
{
	gfloat len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	gfloat y = len > 0.0f ? d[1] / len : 0.0f;
	gfloat t = sqrtf (fabsf (y));
	const gfloat *top = y >= 0.0f ? zenith : ground;
	gint   k;

	for (k = 0; k < 3; k++)
		out[k] = to_linear (horizon[k]) + (to_linear (top[k]) - to_linear (horizon[k])) * t;
}

/* Linear radiance of the source along @d (no rotation, no exposure). */
static void
sky_eval (LrgEnvironment *self,
          const gfloat   *d,
          gfloat         *out)
{
	switch (self->source)
	{
	case LRG_ENVIRONMENT_SKY_COLOR:
		out[0] = to_linear (self->sky_color[0]);
		out[1] = to_linear (self->sky_color[1]);
		out[2] = to_linear (self->sky_color[2]);
		return;
	case LRG_ENVIRONMENT_SKY_DOME:
		if (self->dome != NULL)
		{
			GrlVector3 dir;
			GrlColor   c;

			dir.x = d[0];
			dir.y = d[1];
			dir.z = d[2];
			lrg_sky_dome_get_color_at (self->dome, &dir, &c);
			out[0] = to_linear ((gfloat)c.r / 255.0f);
			out[1] = to_linear ((gfloat)c.g / 255.0f);
			out[2] = to_linear ((gfloat)c.b / 255.0f);
			return;
		}
		break;
	case LRG_ENVIRONMENT_SKY_CUBEMAP:
		if (faces_complete (self))
		{
			gfloat s;
			gfloat t;
			guint  f = direction_to_face (d, &s, &t);

			sample_face_clamped (self->faces[f], s, t, out);
			return;
		}
		break;
	case LRG_ENVIRONMENT_SKY_EQUIRECT:
		if (self->equirect != NULL)
		{
			lrg_hdr_image_sample_direction (self->equirect, d, out);
			return;
		}
		break;
	case LRG_ENVIRONMENT_SKY_GRADIENT:
	default:
		break;
	}
	gradient_eval (self->zenith, self->horizon, self->ground, d, out);
}

void
lrg_environment_sample_sky (LrgEnvironment *self,
                            const gfloat   *direction,
                            gfloat         *out_rgb)
{
	gfloat d[3];
	gfloat k;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (direction != NULL && out_rgb != NULL);

	rotate_lookup (effective_rotation (self), direction, d);
	sky_eval (self, d, out_rgb);
	k = exposure_factor (self);
	out_rgb[0] *= k;
	out_rgb[1] *= k;
	out_rgb[2] *= k;
}

/* Renders the source into six @size faces of linear RGB on the CPU.  A
 * large panorama is box-reduced first so the faces do not alias. */
static gfloat *
render_faces_cpu (LrgEnvironment *self,
                  guint           size)
{
	gfloat      *faces = g_new (gfloat, (gsize)6 * size * size * 3);
	LrgHdrImage *saved = NULL;
	guint        f;
	guint        x;
	guint        y;

	if (self->source == LRG_ENVIRONMENT_SKY_EQUIRECT && self->equirect != NULL &&
	    lrg_hdr_image_get_width (self->equirect) > size * 4)
	{
		saved = self->equirect;
		self->equirect = lrg_hdr_image_resize (saved, size * 4, size * 2);
	}
	for (f = 0; f < 6; f++)
	{
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				gfloat d[3];

				lrg_environment_cube_face_direction (f, ((gfloat)x + 0.5f) / (gfloat)size,
				                                     ((gfloat)y + 0.5f) / (gfloat)size, d);
				sky_eval (self, d, faces + (((gsize)f * size + y) * size + x) * 3);
			}
		}
	}
	if (saved != NULL)
	{
		g_object_unref (self->equirect);
		self->equirect = saved;
	}
	return faces;
}

/* the 9 real L2 basis functions at unit @d */
static void
sh_basis (const gfloat *d,
          gfloat       *y)
{
	gfloat x = d[0];
	gfloat yy = d[1];
	gfloat z = d[2];

	y[0] = 0.282095f;
	y[1] = 0.488603f * yy;
	y[2] = 0.488603f * z;
	y[3] = 0.488603f * x;
	y[4] = 1.092548f * x * yy;
	y[5] = 1.092548f * yy * z;
	y[6] = 0.315392f * (3.0f * z * z - 1.0f);
	y[7] = 1.092548f * x * z;
	y[8] = 0.546274f * (x * x - yy * yy);
}

void
lrg_environment_sh_project_cubemap (const gfloat *faces,
                                    guint         size,
                                    gfloat       *out_sh)
{
	/* clamped-cosine convolution (A0, A1, A2) divided by pi */
	static const gdouble band[9] = { 1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0,
	                                 0.25, 0.25, 0.25, 0.25, 0.25 };
	gdouble acc[LRG_ENVIRONMENT_SH_FLOATS];
	gdouble total = 0.0;
	guint   f;
	guint   x;
	guint   y;
	gint    i;

	g_return_if_fail (faces != NULL);
	g_return_if_fail (size > 0);
	g_return_if_fail (out_sh != NULL);

	memset (acc, 0, sizeof (acc));
	for (f = 0; f < 6; f++)
	{
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				gfloat  s = ((gfloat)x + 0.5f) / (gfloat)size;
				gfloat  t = ((gfloat)y + 0.5f) / (gfloat)size;
				gfloat  u = s * 2.0f - 1.0f;
				gfloat  v = t * 2.0f - 1.0f;
				gfloat  d[3];
				gfloat  b[9];
				gdouble w;
				const gfloat *c = faces + (((gsize)f * size + y) * size + x) * 3;

				/* solid angle of the texel: (2/N)^2 / (1 + u^2 + v^2)^1.5 */
				w = (4.0 / ((gdouble)size * size)) / pow (1.0 + u * u + v * v, 1.5);
				lrg_environment_cube_face_direction (f, s, t, d);
				sh_basis (d, b);
				for (i = 0; i < 9; i++)
				{
					acc[i * 3 + 0] += c[0] * b[i] * w;
					acc[i * 3 + 1] += c[1] * b[i] * w;
					acc[i * 3 + 2] += c[2] * b[i] * w;
				}
				total += w;
			}
		}
	}
	/* normalize the discrete solid angles to exactly 4 pi */
	for (i = 0; i < LRG_ENVIRONMENT_SH_FLOATS; i++)
		out_sh[i] = (gfloat)(acc[i] * (4.0 * ENV_PI / total) * band[i / 3]);
}

void
lrg_environment_sh_irradiance (const gfloat *sh,
                               const gfloat *normal,
                               gfloat       *out_rgb)
{
	gfloat b[9];
	gint   i;
	gint   c;

	g_return_if_fail (sh != NULL && normal != NULL && out_rgb != NULL);

	sh_basis (normal, b);
	for (c = 0; c < 3; c++)
	{
		gfloat v = 0.0f;

		for (i = 0; i < 9; i++)
			v += sh[i * 3 + c] * b[i];
		out_rgb[c] = MAX (v, 0.0f);
	}
}

void
lrg_environment_compute_sh (LrgEnvironment *self,
                            guint           face_size,
                            gfloat         *out_sh)
{
	g_autofree gfloat *faces = NULL;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));

	face_size = CLAMP (face_size, 4u, 256u);
	faces = render_faces_cpu (self, face_size);
	lrg_environment_sh_project_cubemap (faces, face_size, self->sh);
	if (out_sh != NULL)
		memcpy (out_sh, self->sh, sizeof (self->sh));
}

void
lrg_environment_get_sh (LrgEnvironment *self,
                        gfloat         *out_sh)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (out_sh != NULL);
	memcpy (out_sh, self->sh, sizeof (self->sh));
}

void
lrg_environment_sample_diffuse (LrgEnvironment *self,
                                const gfloat   *normal,
                                gfloat         *out_rgb)
{
	gfloat d[3];
	gfloat len;
	gfloat k;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (normal != NULL && out_rgb != NULL);

	len = sqrtf (normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
	vec3_set (d, normal[0] / MAX (len, 1e-6f), normal[1] / MAX (len, 1e-6f), normal[2] / MAX (len, 1e-6f));
	rotate_lookup (effective_rotation (self), d, d);
	lrg_environment_sh_irradiance (self->sh, d, out_rgb);
	k = exposure_factor (self) * self->diffuse_intensity;
	out_rgb[0] *= k;
	out_rgb[1] *= k;
	out_rgb[2] *= k;
}

/* Van der Corput radical inverse in base 2 (bit reversal) */
static gfloat
radical_inverse (guint32 bits)
{
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
	bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
	bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
	bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
	return (gfloat)((gdouble)bits * 2.3283064365386963e-10);
}

void
lrg_environment_integrate_brdf (gfloat  n_dot_v,
                                gfloat  roughness,
                                guint   samples,
                                gfloat *out_scale,
                                gfloat *out_bias)
{
	gdouble a_sum = 0.0;
	gdouble b_sum = 0.0;
	gfloat  nv;
	gfloat  alpha;
	gfloat  k;
	gfloat  v[3];
	guint   i;

	g_return_if_fail (out_scale != NULL && out_bias != NULL);

	samples = MAX (samples, 1u);
	nv = CLAMP (n_dot_v, 1e-4f, 1.0f);
	roughness = CLAMP (roughness, 0.0f, 1.0f);
	alpha = roughness * roughness;
	k = alpha / 2.0f;
	/* view in the x/z plane, normal = +Z */
	vec3_set (v, sqrtf (1.0f - nv * nv), 0.0f, nv);

	for (i = 0; i < samples; i++)
	{
		gfloat xi1 = ((gfloat)i + 0.5f) / (gfloat)samples;
		gfloat xi2 = radical_inverse (i);
		gfloat phi = 2.0f * (gfloat)ENV_PI * xi1;
		/* GGX importance sample of the half vector */
		gfloat cos_t = sqrtf ((1.0f - xi2) / (1.0f + (alpha * alpha - 1.0f) * xi2));
		gfloat sin_t = sqrtf (MAX (1.0f - cos_t * cos_t, 0.0f));
		gfloat h[3];
		gfloat l[3];
		gfloat vh;
		gfloat nl;
		gfloat nh;

		vec3_set (h, cosf (phi) * sin_t, sinf (phi) * sin_t, cos_t);
		vh = v[0] * h[0] + v[1] * h[1] + v[2] * h[2];
		vec3_set (l, 2.0f * vh * h[0] - v[0], 2.0f * vh * h[1] - v[1], 2.0f * vh * h[2] - v[2]);
		nl = l[2];
		nh = MAX (h[2], 0.0f);
		vh = MAX (vh, 0.0f);
		if (nl > 0.0f && nh > 0.0f)
		{
			/* Smith-Schlick G with k = alpha / 2 (the IBL remapping) */
			gfloat g = (nv / (nv * (1.0f - k) + k)) * (nl / (nl * (1.0f - k) + k));
			gfloat g_vis = g * vh / (nh * nv);
			gfloat fc = powf (1.0f - vh, 5.0f);

			a_sum += (1.0f - fc) * g_vis;
			b_sum += fc * g_vis;
		}
	}
	*out_scale = (gfloat)(a_sum / (gdouble)samples);
	*out_bias = (gfloat)(b_sum / (gdouble)samples);
}

gfloat *
lrg_environment_compute_brdf_lut (guint  size,
                                  guint  samples,
                                  gsize *n_floats)
{
	gfloat *lut;
	guint   x;
	guint   y;

	size = MAX (size, 2u);
	lut = g_new (gfloat, (gsize)size * size * 2);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
		{
			gfloat *e = lut + ((gsize)y * size + x) * 2;

			lrg_environment_integrate_brdf (((gfloat)x + 0.5f) / (gfloat)size,
			                                ((gfloat)y + 0.5f) / (gfloat)size,
			                                samples, &e[0], &e[1]);
		}
	}
	if (n_floats != NULL)
		*n_floats = (gsize)size * size * 2;
	return lut;
}

/* ==========================================================================
 * GPU helpers
 * ========================================================================== */

#define GLSL_FULLSCREEN_VS \
	"#version 330\n" \
	"in vec3 vertexPosition;\n" \
	"out vec2 fragUv;\n" \
	"void main() {\n" \
	"    fragUv = vertexPosition.xy;\n" \
	"    gl_Position = vec4(vertexPosition.xy * 2.0 - 1.0, 0.0, 1.0);\n" \
	"}\n"

#define GLSL_FACE_DIR \
	"vec3 lrg_face_dir(int face, vec2 uv) {\n" \
	"    vec2 c = uv * 2.0 - 1.0;\n" \
	"    vec3 d;\n" \
	"    if (face == 0) d = vec3(1.0, -c.y, -c.x);\n" \
	"    else if (face == 1) d = vec3(-1.0, -c.y, c.x);\n" \
	"    else if (face == 2) d = vec3(c.x, 1.0, c.y);\n" \
	"    else if (face == 3) d = vec3(c.x, -1.0, -c.y);\n" \
	"    else if (face == 4) d = vec3(c.x, -c.y, 1.0);\n" \
	"    else d = vec3(-c.x, -c.y, -1.0);\n" \
	"    return normalize(d);\n" \
	"}\n"

/* copy / downsample / decode / readback of one cube face */
static const gchar *copy_fs =
	"#version 330\n"
	"in vec2 fragUv;\n"
	"out vec4 finalColor;\n"
	"uniform samplerCube srcMap;\n"
	"uniform int face;\n"
	"uniform float lod;\n"
	"uniform int decode;\n"
	GLSL_FACE_DIR
	"void main() {\n"
	"    vec3 c = textureLod(srcMap, lrg_face_dir(face, fragUv), lod).rgb;\n"
	"    if (decode != 0) c = pow(max(c, vec3(0.0)), vec3(2.2));\n"
	"    finalColor = vec4(c, 1.0);\n"
	"}\n";

/* GGX prefilter with per-sample source level (Karis' filtered
 * importance sampling) over a chain of box-reduced cubemaps */
static const gchar *prefilter_fs =
	"#version 330\n"
	"in vec2 fragUv;\n"
	"out vec4 finalColor;\n"
	"uniform samplerCube chain0;\n"
	"uniform samplerCube chain1;\n"
	"uniform samplerCube chain2;\n"
	"uniform samplerCube chain3;\n"
	"uniform samplerCube chain4;\n"
	"uniform samplerCube chain5;\n"
	"uniform samplerCube chain6;\n"
	"uniform samplerCube chain7;\n"
	"uniform int chainCount;\n"
	"uniform int face;\n"
	"uniform float roughness;\n"
	"uniform float srcSize;\n"
	"uniform int sampleCount;\n"
	"const float PI = 3.14159265;\n"
	GLSL_FACE_DIR
	"vec3 chainSample(int i, vec3 d) {\n"
	"    if (i <= 0) return textureLod(chain0, d, 0.0).rgb;\n"
	"    if (i == 1) return textureLod(chain1, d, 0.0).rgb;\n"
	"    if (i == 2) return textureLod(chain2, d, 0.0).rgb;\n"
	"    if (i == 3) return textureLod(chain3, d, 0.0).rgb;\n"
	"    if (i == 4) return textureLod(chain4, d, 0.0).rgb;\n"
	"    if (i == 5) return textureLod(chain5, d, 0.0).rgb;\n"
	"    if (i == 6) return textureLod(chain6, d, 0.0).rgb;\n"
	"    return textureLod(chain7, d, 0.0).rgb;\n"
	"}\n"
	"vec3 chainLod(vec3 d, float lod) {\n"
	"    lod = clamp(lod, 0.0, float(chainCount - 1));\n"
	"    int i = int(floor(lod));\n"
	"    float f = lod - float(i);\n"
	"    vec3 a = chainSample(i, d);\n"
	"    if (f < 0.001 || i + 1 >= chainCount) return a;\n"
	"    return mix(a, chainSample(i + 1, d), f);\n"
	"}\n"
	"float radicalInverse(uint bits) {\n"
	"    bits = (bits << 16u) | (bits >> 16u);\n"
	"    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
	"    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
	"    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
	"    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
	"    return float(bits) * 2.3283064365386963e-10;\n"
	"}\n"
	"void main() {\n"
	"    vec3 n = lrg_face_dir(face, fragUv);\n"
	"    if (roughness <= 0.0) { finalColor = vec4(chainSample(0, n), 1.0); return; }\n"
	"    float a = roughness * roughness;\n"
	"    float a2 = a * a;\n"
	"    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
	"    vec3 tx = normalize(cross(up, n));\n"
	"    vec3 ty = cross(n, tx);\n"
	"    float saTexel = 4.0 * PI / (6.0 * srcSize * srcSize);\n"
	"    vec3 sum = vec3(0.0);\n"
	"    float wsum = 0.0;\n"
	"    for (int i = 0; i < sampleCount; i++) {\n"
	"        float xi1 = (float(i) + 0.5) / float(sampleCount);\n"
	"        float xi2 = radicalInverse(uint(i));\n"
	"        float phi = 2.0 * PI * xi1;\n"
	"        float ct = sqrt((1.0 - xi2) / (1.0 + (a2 - 1.0) * xi2));\n"
	"        float st = sqrt(max(1.0 - ct * ct, 0.0));\n"
	"        vec3 h = normalize(tx * (cos(phi) * st) + ty * (sin(phi) * st) + n * ct);\n"
	"        vec3 l = normalize(2.0 * dot(n, h) * h - n);\n"
	"        float nl = dot(n, l);\n"
	"        if (nl <= 0.0) continue;\n"
	"        float nh = max(dot(n, h), 0.0);\n"
	"        float dd = nh * nh * (a2 - 1.0) + 1.0;\n"
	"        float D = a2 / (PI * dd * dd);\n"
	"        float pdf = D * 0.25 + 1e-4;\n"
	"        float saSample = 1.0 / (float(sampleCount) * pdf + 1e-4);\n"
	"        float lod = 0.5 * log2(saSample / saTexel) + 1.0;\n"
	"        sum += chainLod(l, lod) * nl;\n"
	"        wsum += nl;\n"
	"    }\n"
	"    finalColor = vec4(sum / max(wsum, 1e-4), 1.0);\n"
	"}\n";

/* Compiles a full-screen pass; FALSE (with a warning) when GLSL 330 fails. */
static gboolean
pass_shader_load (const gchar *fs,
                  Shader      *out)
{
	*out = LoadShaderFromMemory (GLSL_FULLSCREEN_VS, fs);
	if (out->id == 0 || out->id == rlGetShaderIdDefault ())
	{
		g_warning ("environment: a GPU pass shader did not compile (GLSL 330 needed)");
		return FALSE;
	}
	return TRUE;
}

static void
pass_shader_unload (Shader *shader)
{
	if (shader->id != 0 && shader->id != rlGetShaderIdDefault ())
		UnloadShader (*shader);
	memset (shader, 0, sizeof (*shader));
}

/* Draws the unit quad (0..1) the full-screen vertex shader stretches
 * over the viewport, then flushes so uniforms may change. */
static void
draw_quad (void)
{
	rlBegin (RL_QUADS);
	rlColor4ub (255, 255, 255, 255);
	rlVertex3f (0.0f, 0.0f, 0.0f);
	rlVertex3f (1.0f, 0.0f, 0.0f);
	rlVertex3f (1.0f, 1.0f, 0.0f);
	rlVertex3f (0.0f, 1.0f, 0.0f);
	rlEnd ();
	rlDrawRenderBatchActive ();
}

/* Saved rlgl state around a GPU job. */
typedef struct
{
	guint  fbo;
	Matrix projection;
	Matrix modelview;
} GpuState;

static void
gpu_begin (GpuState *st)
{
	rlDrawRenderBatchActive ();
	st->fbo = rlGetActiveFramebuffer ();
	st->projection = rlGetMatrixProjection ();
	st->modelview = rlGetMatrixModelview ();
	rlDisableColorBlend ();
}

static void
gpu_end (GpuState *st)
{
	rlDrawRenderBatchActive ();
	if (st->fbo != 0)
		rlEnableFramebuffer (st->fbo);
	else
		rlDisableFramebuffer ();
	rlViewport (0, 0, rlGetFramebufferWidth (), rlGetFramebufferHeight ());
	rlSetMatrixProjection (st->projection);
	rlSetMatrixModelview (st->modelview);
	rlEnableColorBlend ();
	rlEnableDepthTest ();
	rlEnableDepthMask ();
	rlEnableBackfaceCulling ();
	rlActiveTextureSlot (0);
}

/* Attaches @face of @mip of @cube (or the 2D @cube when @face < 0) as the
 * colour target and makes it current. */
static gboolean
target_bind (guint fbo,
             guint texture,
             gint  face,
             guint mip,
             guint size)
{
	rlFramebufferAttach (fbo, texture, RL_ATTACHMENT_COLOR_CHANNEL0,
	                     face < 0 ? RL_ATTACHMENT_TEXTURE2D : RL_ATTACHMENT_CUBEMAP_POSITIVE_X + face,
	                     (gint)mip);
	if (!rlFramebufferComplete (fbo))
		return FALSE;
	rlEnableFramebuffer (fbo);
	rlViewport (0, 0, (gint)size, (gint)size);
	rlDisableDepthTest ();
	rlDisableBackfaceCulling ();
	return TRUE;
}

guint
lrg_env_mip_count_for (guint size)
{
	guint n = 1;

	while ((size >> n) >= 1 && n < 16)
		n++;
	return n;
}

guint
lrg_env_gpu_cube_new (guint size,
                      guint mips)
{
	gsize   total = 0;
	guint   m;
	guint   id;
	guint8 *zero;

	/* raylib refuses empty RGBA16F cubemaps; upload zeros instead */
	for (m = 0; m < mips; m++)
	{
		gsize s = MAX (size >> m, 1u);

		total += s * s * 6 * 8;
	}
	zero = g_malloc0 (total);
	id = rlLoadTextureCubemap (zero, (gint)size, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, (gint)mips);
	g_free (zero);
	return id;
}

guint
lrg_env_gpu_cube_from_faces (const gfloat *faces,
                             guint         size)
{
	gsize    n = (gsize)size * size * 6;
	guint16 *half = g_new (guint16, n * 4);
	gsize    i;
	guint    id;

	for (i = 0; i < n; i++)
	{
		half[i * 4 + 0] = float_to_half (faces[i * 3 + 0]);
		half[i * 4 + 1] = float_to_half (faces[i * 3 + 1]);
		half[i * 4 + 2] = float_to_half (faces[i * 3 + 2]);
		half[i * 4 + 3] = float_to_half (1.0f);
	}
	id = rlLoadTextureCubemap (half, (gint)size, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
	g_free (half);
	return id;
}

void
lrg_env_gpu_unload (guint texture)
{
	if (texture != 0)
		rlUnloadTexture (texture);
}

/* Runs the copy shader from @src (lod @lod, decoded when @decode) into
 * face @face (or a 2D target when < 0) of @dst at @mip. */
static gboolean
copy_pass (Shader *sh,
           guint   fbo,
           guint   src,
           gfloat  lod,
           gboolean decode,
           guint   dst,
           gint    face_dst,
           gint    face_src,
           guint   mip,
           guint   size)
{
	gint unit = 1;
	gint dec = decode ? 1 : 0;

	if (!target_bind (fbo, dst, face_dst, mip, size))
		return FALSE;
	BeginShaderMode (*sh);
	SetShaderValue (*sh, GetShaderLocation (*sh, "srcMap"), &unit, SHADER_UNIFORM_INT);
	SetShaderValue (*sh, GetShaderLocation (*sh, "face"), &face_src, SHADER_UNIFORM_INT);
	SetShaderValue (*sh, GetShaderLocation (*sh, "lod"), &lod, SHADER_UNIFORM_FLOAT);
	SetShaderValue (*sh, GetShaderLocation (*sh, "decode"), &dec, SHADER_UNIFORM_INT);
	rlActiveTextureSlot (1);
	rlEnableTextureCubemap (src);
	draw_quad ();
	rlActiveTextureSlot (1);
	rlDisableTextureCubemap ();
	rlActiveTextureSlot (0);
	EndShaderMode ();
	return TRUE;
}

guint
lrg_env_gpu_capture (guint              size,
                     const gfloat      *position,
                     gfloat             near_plane,
                     gfloat             far_plane,
                     const gfloat      *clear_rgb,
                     LrgEnvCaptureFunc  func,
                     gpointer           user_data,
                     GError           **error)
{
	/* forward and up of each face (the OpenGL cubemap orientation) */
	static const gfloat fwd[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
	                                  { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	static const gfloat up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
	                                 { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
	GpuState st;
	Shader   copy;
	guint    raw;
	guint    lin;
	guint    depth;
	guint    fbo;
	gfloat   proj[16];
	guint    f;
	gboolean ok = TRUE;

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "No GL context");
		return 0;
	}
	if (!pass_shader_load (copy_fs, &copy))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "Pass shader failed");
		return 0;
	}

	gpu_begin (&st);
	raw = lrg_env_gpu_cube_new (size, 1);
	lin = lrg_env_gpu_cube_new (size, 1);
	depth = rlLoadTextureDepth ((gint)size, (gint)size, true);
	fbo = rlLoadFramebuffer ();
	rlFramebufferAttach (fbo, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
	lrg_math3d_perspective ((gfloat)(ENV_PI / 2.0), 1.0f, near_plane, far_plane, proj);

	/* 1. draw the six faces */
	for (f = 0; f < 6 && ok; f++)
	{
		gfloat target[3];
		gfloat view[16];

		target[0] = position[0] + fwd[f][0];
		target[1] = position[1] + fwd[f][1];
		target[2] = position[2] + fwd[f][2];
		lrg_math3d_look_at (position, target, up[f], view);

		if (!target_bind (fbo, raw, (gint)f, 0, size))
		{
			ok = FALSE;
			break;
		}
		rlClearColor (color_byte (powf (MAX (clear_rgb[0], 0.0f), 1.0f / 2.2f)),
		              color_byte (powf (MAX (clear_rgb[1], 0.0f), 1.0f / 2.2f)),
		              color_byte (powf (MAX (clear_rgb[2], 0.0f), 1.0f / 2.2f)), 255);
		rlClearScreenBuffers ();
		rlSetMatrixProjection (matrix_from_floats (proj));
		rlSetMatrixModelview (matrix_from_floats (view));
		rlEnableDepthTest ();
		rlEnableDepthMask ();
		rlEnableColorBlend ();
		if (func != NULL)
			func (f, view, proj, user_data);
		rlDrawRenderBatchActive ();
		rlDisableColorBlend ();
	}
	rlSetMatrixProjection (st.projection);
	rlSetMatrixModelview (st.modelview);

	/* the capture framebuffer frees its depth renderbuffer with it
	 * (rlUnloadFramebuffer() deletes the depth attachment; never delete
	 * a renderbuffer id with rlUnloadTexture(): that frees a texture of
	 * the same number) */
	rlUnloadFramebuffer (fbo);
	(void)depth;

	/* 2. decode the display-encoded capture to linear radiance */
	fbo = rlLoadFramebuffer ();
	for (f = 0; f < 6 && ok; f++)
		ok = copy_pass (&copy, fbo, raw, 0.0f, TRUE, lin, (gint)f, (gint)f, 0, size);
	rlUnloadFramebuffer (fbo);
	lrg_env_gpu_unload (raw);
	gpu_end (&st);
	pass_shader_unload (&copy);

	if (!ok)
	{
		lrg_env_gpu_unload (lin);
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "The capture framebuffer is incomplete");
		return 0;
	}
	return lin;
}

guint
lrg_env_gpu_prefilter (guint     radiance,
                       guint     radiance_size,
                       guint     out_size,
                       guint     out_mips,
                       GError  **error)
{
	static const gchar *names[ENV_CHAIN_MAX] = { "chain0", "chain1", "chain2", "chain3",
	                                             "chain4", "chain5", "chain6", "chain7" };
	GpuState st;
	Shader   copy;
	Shader   pre;
	guint    chain[ENV_CHAIN_MAX];
	guint    count = 1;
	guint    out;
	guint    fbo;
	guint    m;
	guint    f;
	guint    i;
	gboolean ok = TRUE;

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "No GL context");
		return 0;
	}
	if (!pass_shader_load (copy_fs, &copy))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "Pass shader failed");
		return 0;
	}
	if (!pass_shader_load (prefilter_fs, &pre))
	{
		pass_shader_unload (&copy);
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "Prefilter shader failed");
		return 0;
	}

	gpu_begin (&st);
	fbo = rlLoadFramebuffer ();

	/* 1. the source chain: each level a 2x box reduction of the previous
	 *    (one bilinear tap at the shared corner of four texels) */
	chain[0] = radiance;
	while (count < ENV_CHAIN_MAX && (radiance_size >> count) >= 4 && ok)
	{
		guint s = radiance_size >> count;

		chain[count] = lrg_env_gpu_cube_new (s, 1);
		for (f = 0; f < 6 && ok; f++)
			ok = copy_pass (&copy, fbo, chain[count - 1], 0.0f, FALSE, chain[count], (gint)f, (gint)f, 0, s);
		count++;
	}

	/* 2. one roughness per mip of the output */
	out = lrg_env_gpu_cube_new (out_size, out_mips);
	for (m = 0; m < out_mips && ok; m++)
	{
		guint  s = MAX (out_size >> m, 1u);
		gfloat rough = out_mips > 1 ? (gfloat)m / (gfloat)(out_mips - 1) : 0.0f;
		gfloat src_size = (gfloat)radiance_size;
		gint   samples = ENV_PREFILTER_SAMPLES;
		gint   n = (gint)count;

		for (f = 0; f < 6 && ok; f++)
		{
			gint face = (gint)f;

			if (!target_bind (fbo, out, face, m, s))
			{
				ok = FALSE;
				break;
			}
			BeginShaderMode (pre);
			for (i = 0; i < ENV_CHAIN_MAX; i++)
			{
				gint unit = (gint)i + 1;

				SetShaderValue (pre, GetShaderLocation (pre, names[i]), &unit, SHADER_UNIFORM_INT);
				rlActiveTextureSlot (unit);
				rlEnableTextureCubemap (chain[MIN (i, count - 1)]);
			}
			SetShaderValue (pre, GetShaderLocation (pre, "chainCount"), &n, SHADER_UNIFORM_INT);
			SetShaderValue (pre, GetShaderLocation (pre, "face"), &face, SHADER_UNIFORM_INT);
			SetShaderValue (pre, GetShaderLocation (pre, "roughness"), &rough, SHADER_UNIFORM_FLOAT);
			SetShaderValue (pre, GetShaderLocation (pre, "srcSize"), &src_size, SHADER_UNIFORM_FLOAT);
			SetShaderValue (pre, GetShaderLocation (pre, "sampleCount"), &samples, SHADER_UNIFORM_INT);
			draw_quad ();
			for (i = 0; i < ENV_CHAIN_MAX; i++)
			{
				rlActiveTextureSlot ((gint)i + 1);
				rlDisableTextureCubemap ();
			}
			rlActiveTextureSlot (0);
			EndShaderMode ();
		}
	}

	for (i = 1; i < count; i++)
		lrg_env_gpu_unload (chain[i]);
	rlUnloadFramebuffer (fbo);
	gpu_end (&st);
	pass_shader_unload (&copy);
	pass_shader_unload (&pre);

	if (!ok)
	{
		lrg_env_gpu_unload (out);
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "A prefilter framebuffer is incomplete");
		return 0;
	}
	return out;
}

gfloat *
lrg_env_gpu_read_face (guint cube,
                       guint size,
                       guint mip,
                       guint face)
{
	GpuState st;
	Shader   copy;
	guint    s;
	guint    tex;
	guint    fbo;
	gfloat  *rgba;
	gfloat  *rgb = NULL;
	gsize    i;

	if (cube == 0 || face >= 6 || !IsWindowReady ())
		return NULL;
	s = MAX (size >> mip, 1u);
	if (!pass_shader_load (copy_fs, &copy))
		return NULL;

	gpu_begin (&st);
	tex = rlLoadTexture (NULL, (gint)s, (gint)s, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
	fbo = rlLoadFramebuffer ();
	if (tex != 0 && copy_pass (&copy, fbo, cube, (gfloat)mip, FALSE, tex, -1, (gint)face, 0, s))
	{
		rlDrawRenderBatchActive ();
		rgba = rlReadTexturePixels (tex, (gint)s, (gint)s, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
		if (rgba != NULL)
		{
			rgb = g_new (gfloat, (gsize)s * s * 3);
			for (i = 0; i < (gsize)s * s; i++)
			{
				rgb[i * 3 + 0] = rgba[i * 4 + 0];
				rgb[i * 3 + 1] = rgba[i * 4 + 1];
				rgb[i * 3 + 2] = rgba[i * 4 + 2];
			}
			MemFree (rgba);
		}
	}
	rlUnloadFramebuffer (fbo);
	lrg_env_gpu_unload (tex);
	gpu_end (&st);
	pass_shader_unload (&copy);
	return rgb;
}

/* ==========================================================================
 * Bake
 * ========================================================================== */

static void
dome_capture_cb (guint         face,
                 const gfloat *view,
                 const gfloat *projection,
                 gpointer      user_data)
{
	GrlVector3 eye = { 0.0f, 0.0f, 0.0f };

	lrg_sky_dome_draw (LRG_SKY_DOME (user_data), &eye);
}

/* The radiance cubemap of the source; fills self->sh. */
static guint
bake_radiance (LrgEnvironment  *self,
               guint            size,
               GError         **error)
{
	if (self->source == LRG_ENVIRONMENT_SKY_DOME && self->dome != NULL)
	{
		static const gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
		static const gfloat black[3] = { 0.0f, 0.0f, 0.0f };
		guint    cube;
		guint    rs;
		guint    mip = 0;
		gfloat  *faces;
		guint    f;

		cube = lrg_env_gpu_capture (size, origin, 0.1f,
		                            lrg_sky_dome_get_radius (self->dome) * 4.0f,
		                            black, dome_capture_cb, self->dome, error);
		if (cube == 0)
			return 0;
		/* irradiance from a read-back of a small copy (16 x 16 per face):
		 * read level 0 at full size is fine too, but slower */
		rs = size;
		while (rs > 32)
		{
			rs /= 2;
			mip++;
		}
		/* the single-level radiance has no mips: read the full face */
		rs = size;
		faces = g_new (gfloat, (gsize)6 * rs * rs * 3);
		for (f = 0; f < 6; f++)
		{
			g_autofree gfloat *one = lrg_env_gpu_read_face (cube, rs, 0, f);

			if (one == NULL)
			{
				g_free (faces);
				lrg_env_gpu_unload (cube);
				g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
				             "Cannot read the sky dome capture back");
				return 0;
			}
			memcpy (faces + (gsize)f * rs * rs * 3, one, sizeof (gfloat) * rs * rs * 3);
		}
		(void)mip;
		lrg_environment_sh_project_cubemap (faces, rs, self->sh);
		g_free (faces);
		return cube;
	}
	else
	{
		g_autofree gfloat *faces = NULL;
		guint              cube;

		if ((self->source == LRG_ENVIRONMENT_SKY_EQUIRECT && self->equirect == NULL) ||
		    (self->source == LRG_ENVIRONMENT_SKY_CUBEMAP && !faces_complete (self)))
			g_warning ("environment: the sky images are not loaded; baking the gradient");
		faces = render_faces_cpu (self, size);
		lrg_environment_sh_project_cubemap (faces, size, self->sh);
		cube = lrg_env_gpu_cube_from_faces (faces, size);
		if (cube == 0)
			g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
			             "Cannot create the radiance cubemap");
		return cube;
	}
}

gboolean
lrg_environment_bake (LrgEnvironment  *self,
                      GError         **error)
{
	g_autofree gfloat  *lut = NULL;
	g_autofree gfloat  *lut_rgb = NULL;
	guint               size;
	guint               mips;
	guint               radiance;
	guint               specular;
	guint               brdf;
	guint               i;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "Baking an environment needs a window (GL context)");
		return FALSE;
	}

	size = self->resolution;
	mips = lrg_env_mip_count_for (size);
	radiance = bake_radiance (self, size, error);
	if (radiance == 0)
		return FALSE;
	specular = lrg_env_gpu_prefilter (radiance, size, size, mips, error);
	if (specular == 0)
	{
		lrg_env_gpu_unload (radiance);
		return FALSE;
	}

	/* the BRDF table, (scale, bias, 0) per texel */
	lut = lrg_environment_compute_brdf_lut (LRG_ENVIRONMENT_BRDF_LUT_SIZE, ENV_BRDF_SAMPLES, NULL);
	lut_rgb = g_new0 (gfloat, (gsize)LRG_ENVIRONMENT_BRDF_LUT_SIZE * LRG_ENVIRONMENT_BRDF_LUT_SIZE * 3);
	for (i = 0; i < LRG_ENVIRONMENT_BRDF_LUT_SIZE * LRG_ENVIRONMENT_BRDF_LUT_SIZE; i++)
	{
		lut_rgb[i * 3 + 0] = lut[i * 2 + 0];
		lut_rgb[i * 3 + 1] = lut[i * 2 + 1];
	}
	brdf = rlLoadTexture (lut_rgb, LRG_ENVIRONMENT_BRDF_LUT_SIZE, LRG_ENVIRONMENT_BRDF_LUT_SIZE,
	                      RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32, 1);
	if (brdf == 0)
	{
		lrg_env_gpu_unload (radiance);
		lrg_env_gpu_unload (specular);
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "Cannot create the BRDF table");
		return FALSE;
	}
	rlTextureParameters (brdf, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (brdf, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (brdf, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (brdf, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);

	/* swap in the new maps */
	lrg_env_gpu_unload (self->radiance_id);
	lrg_env_gpu_unload (self->specular_id);
	lrg_env_gpu_unload (self->brdf_id);
	self->radiance_id = radiance;
	self->specular_id = specular;
	self->brdf_id = brdf;
	self->baked_size = size;
	self->specular_mips = mips;
	self->dirty = FALSE;
	return TRUE;
}

gboolean
lrg_environment_is_baked (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	return self->specular_id != 0;
}

gboolean
lrg_environment_needs_bake (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), TRUE);
	return self->dirty || self->specular_id == 0;
}

void
lrg_environment_release_gpu (LrgEnvironment *self)
{
	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	release_gpu_internal (self);
	self->dirty = TRUE;
}

guint
lrg_environment_get_radiance_texture_id (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	return self->radiance_id;
}

guint
lrg_environment_get_specular_texture_id (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	return self->specular_id;
}

guint
lrg_environment_get_specular_mip_count (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	return self->specular_mips;
}

guint
lrg_environment_get_brdf_texture_id (LrgEnvironment *self)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), 0);
	return self->brdf_id;
}

gfloat *
lrg_environment_read_specular_face (LrgEnvironment *self,
                                    guint           mip,
                                    guint           face,
                                    guint          *out_size)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);

	if (self->specular_id == 0 || mip >= self->specular_mips || face >= 6)
		return NULL;
	if (out_size != NULL)
		*out_size = MAX (self->baked_size >> mip, 1u);
	return lrg_env_gpu_read_face (self->specular_id, self->baked_size, mip, face);
}

/* ==========================================================================
 * Sky drawing
 * ========================================================================== */

static const gchar *sky_vs =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"uniform mat4 mvp;\n"
	"uniform vec3 skyEye;\n"
	"out vec3 fragDir;\n"
	"void main() {\n"
	"    fragDir = vertexPosition - skyEye;\n"
	"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
	"}\n";

static const gchar *sky_fs =
	"#version 330\n"
	"in vec3 fragDir;\n"
	"out vec4 finalColor;\n"
	"uniform samplerCube skyMap;\n"
	"uniform vec4 skyParams;\n"
	"void main() {\n"
	"    vec3 d = normalize(fragDir);\n"
	"    d = vec3(skyParams.y * d.x - skyParams.z * d.z, d.y, skyParams.z * d.x + skyParams.y * d.z);\n"
	"    vec3 c = textureLod(skyMap, d, 0.0).rgb * skyParams.x;\n"
	"    finalColor = vec4(pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);\n"
	"}\n";

void
lrg_environment_draw_sky (LrgEnvironment   *self,
                          const GrlVector3 *camera_position)
{
	/* the 6 faces of a cube of half size 1 around the eye */
	static const gint quads[6][4][3] = {
		{ { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } },
		{ { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, -1 } },
		{ { -1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 } },
		{ { -1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } },
		{ { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
		{ { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } }
	};
	gfloat eye[3];
	gfloat params[4];
	gfloat a;
	gint   unit = 1;
	guint  f;
	guint  v;

	g_return_if_fail (LRG_IS_ENVIRONMENT (self));
	g_return_if_fail (camera_position != NULL);

	if (self->source == LRG_ENVIRONMENT_SKY_DOME && self->dome != NULL)
	{
		lrg_sky_dome_draw (self->dome, camera_position);
		return;
	}
	if (self->radiance_id == 0 || !IsWindowReady ())
		return;

	if (!self->sky_shader_ok)
	{
		self->sky_shader = LoadShaderFromMemory (sky_vs, sky_fs);
		if (self->sky_shader.id == 0 || self->sky_shader.id == rlGetShaderIdDefault ())
		{
			g_warning ("environment: the sky shader did not compile");
			memset (&self->sky_shader, 0, sizeof (self->sky_shader));
			return;
		}
		self->sky_shader_ok = TRUE;
		self->sky_loc_eye = GetShaderLocation (self->sky_shader, "skyEye");
		self->sky_loc_map = GetShaderLocation (self->sky_shader, "skyMap");
		self->sky_loc_params = GetShaderLocation (self->sky_shader, "skyParams");
	}

	vec3_set (eye, camera_position->x, camera_position->y, camera_position->z);
	a = effective_rotation (self) * (gfloat)(ENV_PI / 180.0);
	params[0] = exposure_factor (self);
	params[1] = cosf (a);
	params[2] = sinf (a);
	params[3] = 0.0f;

	rlDrawRenderBatchActive ();
	rlDisableDepthTest ();
	rlDisableDepthMask ();
	rlDisableBackfaceCulling ();
	BeginShaderMode (self->sky_shader);
	SetShaderValue (self->sky_shader, self->sky_loc_eye, eye, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->sky_shader, self->sky_loc_map, &unit, SHADER_UNIFORM_INT);
	SetShaderValue (self->sky_shader, self->sky_loc_params, params, SHADER_UNIFORM_VEC4);
	rlActiveTextureSlot (1);
	rlEnableTextureCubemap (self->radiance_id);
	rlActiveTextureSlot (0);
	rlBegin (RL_QUADS);
	rlColor4ub (255, 255, 255, 255);
	for (f = 0; f < 6; f++)
		for (v = 0; v < 4; v++)
			rlVertex3f (eye[0] + (gfloat)quads[f][v][0], eye[1] + (gfloat)quads[f][v][1],
			            eye[2] + (gfloat)quads[f][v][2]);
	rlEnd ();
	rlDrawRenderBatchActive ();
	EndShaderMode ();
	rlActiveTextureSlot (1);
	rlDisableTextureCubemap ();
	rlActiveTextureSlot (0);
	rlEnableBackfaceCulling ();
	rlEnableDepthMask ();
	rlEnableDepthTest ();
}

/* ==========================================================================
 * Probes from the sky
 * ========================================================================== */

/* draws the baked sky (or a dome) into a probe face */
static void
fill_probe_cb (guint         face,
               const gfloat *view,
               const gfloat *projection,
               gpointer      user_data)
{
	LrgEnvironment *self = LRG_ENVIRONMENT (user_data);
	GrlVector3      eye;
	gfloat          inv[16];

	/* the eye in world space is the translation of the inverse view */
	if (!lrg_math3d_invert (view, inv))
		return;
	eye.x = inv[12];
	eye.y = inv[13];
	eye.z = inv[14];
	lrg_environment_draw_sky (self, &eye);
}

gboolean
lrg_environment_fill_probe (LrgEnvironment      *self,
                            LrgReflectionProbe  *probe,
                            GError             **error)
{
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (probe), FALSE);

	if (self->radiance_id == 0 && !(self->source == LRG_ENVIRONMENT_SKY_DOME && self->dome != NULL))
	{
		if (!lrg_environment_bake (self, error))
			return FALSE;
	}
	{
		static const gfloat black[3] = { 0.0f, 0.0f, 0.0f };
		gfloat pos[3];
		guint  size = lrg_reflection_probe_get_resolution (probe);
		guint  mips = lrg_env_mip_count_for (size);
		guint  radiance;
		guint  pre;

		lrg_reflection_probe_get_position (probe, pos);
		radiance = lrg_env_gpu_capture (size, pos, 0.05f, 1000.0f, black, fill_probe_cb, self, error);
		if (radiance == 0)
			return FALSE;
		pre = lrg_env_gpu_prefilter (radiance, size, size, mips, error);
		lrg_env_gpu_unload (radiance);
		if (pre == 0)
			return FALSE;
		lrg_reflection_probe_take_texture_private (probe, pre, mips);
		return TRUE;
	}
}

/* ==========================================================================
 * Lit-shader binder
 * ========================================================================== */

typedef struct
{
	gint active;
	gint sh;
	gint spec_map;
	gint brdf_map;
	gint params;
	gint rotation;
	gint probe_count;
	gint probe_map[2];
	gint probe_center;
	gint probe_extent;
	gint probe_params;
	gint reference;
	gint brdf_mode;
} EnvLocs;

static GHashTable *loc_cache = NULL;	/* shader id -> EnvLocs* */

static EnvLocs *
get_locs (guint shader_id)
{
	EnvLocs *l;
	gint     check;

	if (loc_cache == NULL)
		loc_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	l = g_hash_table_lookup (loc_cache, GUINT_TO_POINTER (shader_id));
	check = rlGetLocationUniform (shader_id, "lrgEnvActive");
	/* a re-created program may reuse the id: re-read when it differs */
	if (l != NULL && l->active == check)
		return l;
	l = g_new0 (EnvLocs, 1);
	l->active = check;
	l->sh = rlGetLocationUniform (shader_id, "lrgEnvSh");
	l->spec_map = rlGetLocationUniform (shader_id, "lrgEnvSpecularMap");
	l->brdf_map = rlGetLocationUniform (shader_id, "lrgEnvBrdfLut");
	l->params = rlGetLocationUniform (shader_id, "lrgEnvParams");
	l->rotation = rlGetLocationUniform (shader_id, "lrgEnvRotation");
	l->probe_count = rlGetLocationUniform (shader_id, "lrgEnvProbeCount");
	l->probe_map[0] = rlGetLocationUniform (shader_id, "lrgEnvProbeMap0");
	l->probe_map[1] = rlGetLocationUniform (shader_id, "lrgEnvProbeMap1");
	l->probe_center = rlGetLocationUniform (shader_id, "lrgEnvProbeCenter");
	l->probe_extent = rlGetLocationUniform (shader_id, "lrgEnvProbeExtent");
	l->probe_params = rlGetLocationUniform (shader_id, "lrgEnvProbeParams");
	l->reference = rlGetLocationUniform (shader_id, "lrgEnvReference");
	l->brdf_mode = rlGetLocationUniform (shader_id, "lrgEnvBrdfMode");
	g_hash_table_replace (loc_cache, GUINT_TO_POINTER (shader_id), l);
	return l;
}

static void
set_int (gint loc, gint v)
{
	if (loc >= 0)
		rlSetUniform (loc, &v, RL_SHADER_UNIFORM_INT, 1);
}

void
lrg_environment_bind_private (LrgEnvironment *env,
                              guint           shader_id,
                              gint           *texture_unit)
{
	EnvLocs *l;
	gint     unit;
	gint     spec_unit;
	gint     brdf_unit = 0;
	gint     probe_units[2];
	gint     claimed = 0;
	gboolean active;
	gboolean full_window;
	guint    n_probes = 0;

	if (shader_id == 0 || texture_unit == NULL)
		return;
	l = get_locs (shader_id);
	unit = *texture_unit;
	full_window = lrg_light_set_hook_unit_first () >= LRG_LIGHT_SET_HOOK_UNIT;
	rlEnableShader (shader_id);

	/* the prefiltered map's unit: always claimed, every samplerCube
	 * points at it unless it gets a unit of its own */
	if (lrg_light_set_hook_unit_usable (unit))
	{
		spec_unit = unit;
		claimed = 1;
	}
	else
	{
		spec_unit = lrg_light_set_hook_unit_first ();
	}
	probe_units[0] = spec_unit;
	probe_units[1] = spec_unit;
	active = claimed > 0 && env != NULL && env->ibl_enabled && env->specular_id != 0;

	/* the table and the probes, only with a full hook window */
	if (active && full_window && lrg_light_set_hook_unit_usable (unit + claimed))
	{
		brdf_unit = unit + claimed;
		claimed++;
	}
	if (active && full_window)
	{
		guint i;

		for (i = 0; i < 2 && lrg_light_set_hook_unit_usable (unit + claimed); i++)
		{
			probe_units[i] = unit + claimed;
			claimed++;
		}
	}

	set_int (l->spec_map, spec_unit);
	set_int (l->brdf_map, brdf_unit);
	set_int (l->probe_map[0], probe_units[0]);
	set_int (l->probe_map[1], probe_units[1]);
	set_int (l->active, active ? 1 : 0);

	if (active)
	{
		gfloat k = exposure_factor (env);
		gfloat params[4];
		gfloat rot[2];
		gfloat a = effective_rotation (env) * (gfloat)(ENV_PI / 180.0);
		gfloat centers[6] = { 0, 0, 0, 0, 0, 0 };
		gfloat extents[6] = { 1, 1, 1, 1, 1, 1 };
		gfloat pparams[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		gfloat ref[3];
		LrgReflectionProbe *sel[2] = { NULL, NULL };
		guint  i;

		params[0] = env->diffuse_intensity * k;
		params[1] = env->specular_intensity * k;
		params[2] = (gfloat)(env->specular_mips > 0 ? env->specular_mips - 1 : 0);
		params[3] = 0.0f;
		rot[0] = cosf (a);
		rot[1] = sinf (a);
		if (l->sh >= 0)
			rlSetUniform (l->sh, env->sh, RL_SHADER_UNIFORM_VEC3, 9);
		if (l->params >= 0)
			rlSetUniform (l->params, params, RL_SHADER_UNIFORM_VEC4, 1);
		if (l->rotation >= 0)
			rlSetUniform (l->rotation, rot, RL_SHADER_UNIFORM_VEC2, 1);
		set_int (l->brdf_mode, brdf_unit != 0 ? 0 : 1);
		rlActiveTextureSlot (spec_unit);
		rlEnableTextureCubemap (env->specular_id);
		if (brdf_unit != 0)
		{
			rlActiveTextureSlot (brdf_unit);
			rlEnableTexture (env->brdf_id);
		}

		/* probes: the two strongest at the reference point, the ones that
		 * got a unit of their own */
		lrg_environment_select_probes (env, env->reference, &sel[0], &sel[1]);
		for (i = 0; i < 2; i++)
		{
			gfloat pos[3];
			gfloat ext[3];

			if (sel[i] == NULL || probe_units[i] == spec_unit)
				break;
			lrg_reflection_probe_get_position (sel[i], pos);
			lrg_reflection_probe_get_extents (sel[i], ext);
			centers[n_probes * 3 + 0] = pos[0] - env->origin[0];
			centers[n_probes * 3 + 1] = pos[1] - env->origin[1];
			centers[n_probes * 3 + 2] = pos[2] - env->origin[2];
			vec3_copy (extents + n_probes * 3, ext);
			pparams[n_probes * 4 + 0] = lrg_reflection_probe_get_blend_distance (sel[i]);
			pparams[n_probes * 4 + 1] = lrg_reflection_probe_get_intensity (sel[i]) * env->specular_intensity;
			pparams[n_probes * 4 + 2] = lrg_reflection_probe_get_box_projection (sel[i]) ? 1.0f : 0.0f;
			pparams[n_probes * 4 + 3] = (gfloat)MAX ((gint)lrg_reflection_probe_get_mip_count (sel[i]) - 1, 0);
			rlActiveTextureSlot (probe_units[i]);
			rlEnableTextureCubemap (lrg_reflection_probe_get_texture_id (sel[i]));
			n_probes++;
		}
		if (l->probe_center >= 0)
			rlSetUniform (l->probe_center, centers, RL_SHADER_UNIFORM_VEC3, 2);
		if (l->probe_extent >= 0)
			rlSetUniform (l->probe_extent, extents, RL_SHADER_UNIFORM_VEC3, 2);
		if (l->probe_params >= 0)
			rlSetUniform (l->probe_params, pparams, RL_SHADER_UNIFORM_VEC4, 2);
		ref[0] = env->reference[0] - env->origin[0];
		ref[1] = env->reference[1] - env->origin[1];
		ref[2] = env->reference[2] - env->origin[2];
		if (l->reference >= 0)
			rlSetUniform (l->reference, ref, RL_SHADER_UNIFORM_VEC3, 1);
		rlActiveTextureSlot (0);
	}
	set_int (l->probe_count, (gint)n_probes);

	*texture_unit = unit + claimed;
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static const gchar *top_keys[] = { "sky", "ibl", "ambient", "fog", "volumetric", "post_stack", NULL };
static const gchar *sky_keys[] = { "source", "color", "zenith", "horizon", "ground", "path",
                                   "faces", "exposure", "rotation", NULL };
static const gchar *ibl_keys[] = { "enabled", "diffuse_intensity", "specular_intensity",
                                   "resolution", NULL };
static const gchar *ambient_keys[] = { "color", "intensity", NULL };
static const gchar *fog_keys[] = { "enabled", "type", "color", "density", "start", "end",
                                   "height_base", "height_falloff", NULL };
static const gchar *vol_keys[] = { "enabled", "density", "anisotropy", "steps", "max_distance",
                                   "intensity", NULL };

static gboolean
key_known (const gchar **keys,
           const gchar  *key)
{
	guint i;

	for (i = 0; keys[i] != NULL; i++)
		if (g_strcmp0 (keys[i], key) == 0)
			return TRUE;
	return FALSE;
}

/* keeps the members of @map not in @keys under @group */
static void
collect_extras (LrgEnvironment *self,
                const gchar    *group,
                YamlMapping    *map,
                const gchar   **keys)
{
	guint i;
	guint n = yaml_mapping_get_size (map);

	for (i = 0; i < n; i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *node = yaml_mapping_get_value (map, i);
		EnvExtra    *e;

		if (key == NULL || key_known (keys, key) || node == NULL)
			continue;
		e = g_new0 (EnvExtra, 1);
		e->group = g_strdup (group);
		e->key = g_strdup (key);
		e->node = yaml_node_ref (node);
		g_ptr_array_add (self->extras, e);
	}
}

static void
write_extras (LrgEnvironment *self,
              const gchar    *group,
              YamlMapping    *map)
{
	guint i;

	for (i = 0; i < self->extras->len; i++)
	{
		EnvExtra *e = g_ptr_array_index (self->extras, i);

		if (g_strcmp0 (e->group, group) == 0 && !yaml_mapping_has_member (map, e->key))
			yaml_mapping_set_member (map, e->key, e->node);
	}
}

static gboolean
node_double (YamlNode *node,
             gdouble  *out)
{
	const gchar *s;
	gchar       *end = NULL;
	gdouble      v;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	s = yaml_node_get_scalar (node);
	if (s == NULL)
		return FALSE;
	v = g_ascii_strtod (s, &end);
	if (end == s || !isfinite (v))
		return FALSE;
	*out = v;
	return TRUE;
}

static gfloat
map_float (YamlMapping *map,
           const gchar *key,
           gfloat       fallback)
{
	gdouble v;

	if (map == NULL || !node_double (yaml_mapping_get_member (map, key), &v))
		return fallback;
	return (gfloat)v;
}

static gboolean
map_bool (YamlMapping *map,
          const gchar *key,
          gboolean     fallback)
{
	YamlNode    *node;
	const gchar *s;

	if (map == NULL)
		return fallback;
	node = yaml_mapping_get_member (map, key);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fallback;
	s = yaml_node_get_scalar (node);
	if (s == NULL)
		return fallback;
	if (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0 ||
	    g_ascii_strcasecmp (s, "on") == 0 || g_strcmp0 (s, "1") == 0)
		return TRUE;
	if (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0 ||
	    g_ascii_strcasecmp (s, "off") == 0 || g_strcmp0 (s, "0") == 0)
		return FALSE;
	return fallback;
}

static const gchar *
map_string (YamlMapping *map,
            const gchar *key)
{
	YamlNode *node;

	if (map == NULL)
		return NULL;
	node = yaml_mapping_get_member (map, key);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* reads [r, g, b(, a)] into @out (@n components); FALSE when absent */
static gboolean
map_color (YamlMapping *map,
           const gchar *key,
           gfloat      *out,
           guint        n)
{
	YamlNode     *node;
	YamlSequence *seq;
	guint         i;

	if (map == NULL)
		return FALSE;
	node = yaml_mapping_get_member (map, key);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return FALSE;
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < n && i < yaml_sequence_get_length (seq); i++)
	{
		gdouble v;

		if (node_double (yaml_sequence_get_element (seq, i), &v))
			out[i] = (gfloat)v;
	}
	return TRUE;
}

static YamlMapping *
map_sub (YamlMapping *map,
         const gchar *key)
{
	YamlNode *node = yaml_mapping_get_member (map, key);

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return NULL;
	return yaml_node_get_mapping (node);
}

static void
put_color (YamlMapping  *map,
           const gchar  *key,
           const gfloat *c,
           guint         n)
{
	g_autoptr(YamlSequence) seq = yaml_sequence_new ();
	guint i;

	for (i = 0; i < n; i++)
		yaml_sequence_add_double_element (seq, (gdouble)c[i]);
	yaml_mapping_set_sequence_member (map, key, seq);
}

/* builds a dome from the gradient for a YAML sky_dome source */
static void
ensure_dome (LrgEnvironment *self)
{
	GrlColor z;
	GrlColor h;
	GrlColor g;

	if (self->dome != NULL)
		return;
	self->dome = lrg_sky_dome_new ();
	z.r = color_byte (self->zenith[0]); z.g = color_byte (self->zenith[1]);
	z.b = color_byte (self->zenith[2]); z.a = 255;
	h.r = color_byte (self->horizon[0]); h.g = color_byte (self->horizon[1]);
	h.b = color_byte (self->horizon[2]); h.a = 255;
	g.r = color_byte (self->ground[0]); g.g = color_byte (self->ground[1]);
	g.b = color_byte (self->ground[2]); g.a = 255;
	lrg_sky_dome_set_zenith_color (self->dome, &z);
	lrg_sky_dome_set_horizon_color (self->dome, &h);
	lrg_sky_dome_set_ground_color (self->dome, &g);
}

gboolean
lrg_environment_from_yaml_private (LrgEnvironment  *self,
                                   YamlMapping     *map,
                                   GError         **error)
{
	YamlMapping *sky;
	YamlMapping *ibl;
	YamlMapping *amb;
	YamlMapping *fog;
	YamlMapping *vol;
	YamlNode    *faces_node;
	const gchar *s;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	g_return_val_if_fail (map != NULL, FALSE);

	set_defaults (self);
	g_ptr_array_set_size (self->extras, 0);
	collect_extras (self, NULL, map, top_keys);

	/* sky */
	sky = map_sub (map, "sky");
	if (sky != NULL)
	{
		collect_extras (self, "sky", sky, sky_keys);
		s = map_string (sky, "source");
		if (s != NULL)
		{
			g_autofree gchar *nick = g_strdelimit (g_ascii_strdown (s, -1), "_", '-');
			GEnumClass       *klass = g_type_class_ref (LRG_TYPE_ENVIRONMENT_SKY_SOURCE);
			GEnumValue       *ev = g_enum_get_value_by_nick (klass, nick);

			if (ev == NULL)
			{
				g_type_class_unref (klass);
				g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE,
				             "Unknown sky source '%s'", s);
				return FALSE;
			}
			self->source = (LrgEnvironmentSkySource)ev->value;
			g_type_class_unref (klass);
		}
		map_color (sky, "color", self->sky_color, 3);
		map_color (sky, "zenith", self->zenith, 3);
		map_color (sky, "horizon", self->horizon, 3);
		map_color (sky, "ground", self->ground, 3);
		s = map_string (sky, "path");
		self->sky_path = (s != NULL && *s != '\0') ? g_strdup (s) : NULL;
		faces_node = yaml_mapping_get_member (sky, "faces");
		if (faces_node != NULL && yaml_node_get_node_type (faces_node) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *seq = yaml_node_get_sequence (faces_node);
			guint         i;

			for (i = 0; i < 6 && i < yaml_sequence_get_length (seq); i++)
			{
				const gchar *f = yaml_sequence_get_string_element (seq, i);

				self->face_paths[i] = (f != NULL && *f != '\0') ? g_strdup (f) : NULL;
			}
		}
		self->exposure = map_float (sky, "exposure", 0.0f);
		self->rotation = map_float (sky, "rotation", 0.0f);
		if (self->source == LRG_ENVIRONMENT_SKY_DOME)
			ensure_dome (self);
	}

	/* image-based lighting */
	ibl = map_sub (map, "ibl");
	if (ibl != NULL)
	{
		collect_extras (self, "ibl", ibl, ibl_keys);
		self->ibl_enabled = map_bool (ibl, "enabled", TRUE);
		self->diffuse_intensity = MAX (map_float (ibl, "diffuse_intensity", 1.0f), 0.0f);
		self->specular_intensity = MAX (map_float (ibl, "specular_intensity", 1.0f), 0.0f);
		self->resolution = pot_floor ((guint)MAX (map_float (ibl, "resolution",
		                                                     LRG_ENVIRONMENT_DEFAULT_RESOLUTION), 16.0f),
		                              LRG_ENVIRONMENT_MAX_RESOLUTION);
	}

	/* ambient */
	amb = map_sub (map, "ambient");
	if (amb != NULL)
	{
		collect_extras (self, "ambient", amb, ambient_keys);
		map_color (amb, "color", self->ambient, 3);
		self->ambient_intensity = MAX (map_float (amb, "intensity", 1.0f), 0.0f);
	}

	/* fog */
	fog = map_sub (map, "fog");
	if (fog != NULL)
	{
		collect_extras (self, "fog", fog, fog_keys);
		self->fog_enabled = map_bool (fog, "enabled", TRUE);
		s = map_string (fog, "type");
		if (s != NULL)
		{
			GEnumClass *klass = g_type_class_ref (LRG_TYPE_FOG_TYPE);
			GEnumValue *ev = g_enum_get_value_by_nick (klass, s);

			if (ev == NULL)
			{
				g_type_class_unref (klass);
				g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE,
				             "Unknown fog type '%s'", s);
				return FALSE;
			}
			self->fog_type = (LrgFogType)ev->value;
			g_type_class_unref (klass);
		}
		map_color (fog, "color", self->fog_rgba, 4);
		self->fog_density = MAX (map_float (fog, "density", self->fog_density), 0.0f);
		self->fog_start = map_float (fog, "start", self->fog_start);
		self->fog_end = map_float (fog, "end", self->fog_end);
		self->fog_base = map_float (fog, "height_base", self->fog_base);
		self->fog_falloff = MAX (map_float (fog, "height_falloff", self->fog_falloff), 1e-4f);
	}

	/* volumetrics */
	vol = map_sub (map, "volumetric");
	if (vol != NULL)
	{
		collect_extras (self, "volumetric", vol, vol_keys);
		self->vol_enabled = map_bool (vol, "enabled", TRUE);
		self->vol_density = MAX (map_float (vol, "density", self->vol_density), 0.0f);
		self->vol_anisotropy = CLAMP (map_float (vol, "anisotropy", self->vol_anisotropy), -0.95f, 0.95f);
		self->vol_steps = (guint)CLAMP (map_float (vol, "steps", (gfloat)self->vol_steps), 4.0f, 128.0f);
		self->vol_max_distance = MAX (map_float (vol, "max_distance", self->vol_max_distance), 0.01f);
		self->vol_intensity = MAX (map_float (vol, "intensity", self->vol_intensity), 0.0f);
	}

	s = map_string (map, "post_stack");
	self->post_stack = (s != NULL && *s != '\0') ? g_strdup (s) : NULL;

	emit_changed (self, TRUE);
	return TRUE;
}

YamlMapping *
lrg_environment_to_yaml_private (LrgEnvironment *self)
{
	YamlMapping *map;
	g_autoptr(YamlMapping) sky = yaml_mapping_new ();
	g_autoptr(YamlMapping) ibl = yaml_mapping_new ();
	g_autoptr(YamlMapping) amb = yaml_mapping_new ();
	g_autoptr(YamlMapping) fog = yaml_mapping_new ();
	g_autoptr(YamlMapping) vol = yaml_mapping_new ();
	g_autofree gchar *source = NULL;
	GEnumClass *klass;
	guint       i;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);

	map = yaml_mapping_new ();

	/* sky: the colours always, the references when set */
	klass = g_type_class_ref (LRG_TYPE_ENVIRONMENT_SKY_SOURCE);
	source = g_strdelimit (g_strdup (g_enum_get_value (klass, (gint)self->source)->value_nick), "-", '_');
	g_type_class_unref (klass);
	yaml_mapping_set_string_member (sky, "source", source);
	put_color (sky, "color", self->sky_color, 3);
	put_color (sky, "zenith", self->zenith, 3);
	put_color (sky, "horizon", self->horizon, 3);
	put_color (sky, "ground", self->ground, 3);
	if (self->sky_path != NULL)
		yaml_mapping_set_string_member (sky, "path", self->sky_path);
	for (i = 0; i < 6; i++)
	{
		if (self->face_paths[i] != NULL)
		{
			g_autoptr(YamlSequence) seq = yaml_sequence_new ();
			guint j;

			for (j = 0; j < 6; j++)
				yaml_sequence_add_string_element (seq, self->face_paths[j] != NULL ? self->face_paths[j] : "");
			yaml_mapping_set_sequence_member (sky, "faces", seq);
			break;
		}
	}
	yaml_mapping_set_double_member (sky, "exposure", self->exposure);
	yaml_mapping_set_double_member (sky, "rotation", self->rotation);
	write_extras (self, "sky", sky);
	yaml_mapping_set_mapping_member (map, "sky", sky);

	yaml_mapping_set_boolean_member (ibl, "enabled", self->ibl_enabled);
	yaml_mapping_set_double_member (ibl, "diffuse_intensity", self->diffuse_intensity);
	yaml_mapping_set_double_member (ibl, "specular_intensity", self->specular_intensity);
	yaml_mapping_set_int_member (ibl, "resolution", self->resolution);
	write_extras (self, "ibl", ibl);
	yaml_mapping_set_mapping_member (map, "ibl", ibl);

	put_color (amb, "color", self->ambient, 3);
	yaml_mapping_set_double_member (amb, "intensity", self->ambient_intensity);
	write_extras (self, "ambient", amb);
	yaml_mapping_set_mapping_member (map, "ambient", amb);

	klass = g_type_class_ref (LRG_TYPE_FOG_TYPE);
	yaml_mapping_set_boolean_member (fog, "enabled", self->fog_enabled);
	yaml_mapping_set_string_member (fog, "type", g_enum_get_value (klass, (gint)self->fog_type)->value_nick);
	g_type_class_unref (klass);
	put_color (fog, "color", self->fog_rgba, 4);
	yaml_mapping_set_double_member (fog, "density", self->fog_density);
	yaml_mapping_set_double_member (fog, "start", self->fog_start);
	yaml_mapping_set_double_member (fog, "end", self->fog_end);
	yaml_mapping_set_double_member (fog, "height_base", self->fog_base);
	yaml_mapping_set_double_member (fog, "height_falloff", self->fog_falloff);
	write_extras (self, "fog", fog);
	yaml_mapping_set_mapping_member (map, "fog", fog);

	yaml_mapping_set_boolean_member (vol, "enabled", self->vol_enabled);
	yaml_mapping_set_double_member (vol, "density", self->vol_density);
	yaml_mapping_set_double_member (vol, "anisotropy", self->vol_anisotropy);
	yaml_mapping_set_int_member (vol, "steps", self->vol_steps);
	yaml_mapping_set_double_member (vol, "max_distance", self->vol_max_distance);
	yaml_mapping_set_double_member (vol, "intensity", self->vol_intensity);
	write_extras (self, "volumetric", vol);
	yaml_mapping_set_mapping_member (map, "volumetric", vol);

	if (self->post_stack != NULL)
		yaml_mapping_set_string_member (map, "post_stack", self->post_stack);
	write_extras (self, NULL, map);
	return map;
}

/* the environment mapping inside a parsed document: the root, or its
 * `environment:` member */
static YamlMapping *
document_environment (YamlNode *root)
{
	YamlMapping *map;
	YamlMapping *inner;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return NULL;
	map = yaml_node_get_mapping (root);
	inner = map_sub (map, "environment");
	return inner != NULL ? inner : map;
}

gboolean
lrg_environment_load_from_data (LrgEnvironment  *self,
                                const gchar     *data,
                                gssize           length,
                                GError         **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     parse_error = NULL;
	YamlMapping          *map;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &parse_error))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE,
		             "%s", parse_error != NULL ? parse_error->message : "YAML parse error");
		return FALSE;
	}
	map = document_environment (yaml_parser_get_root (parser));
	if (map == NULL)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE,
		             "The environment block is not a mapping");
		return FALSE;
	}
	return lrg_environment_from_yaml_private (self, map, error);
}

gchar *
lrg_environment_to_data (LrgEnvironment *self)
{
	g_autoptr(YamlGenerator) gen = NULL;
	g_autoptr(YamlMapping)   map = NULL;
	YamlNode                *root;
	gchar                   *out;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), NULL);

	map = lrg_environment_to_yaml_private (self);
	root = yaml_node_new_mapping (map);
	gen = yaml_generator_new ();
	yaml_generator_set_root (gen, root);
	out = yaml_generator_to_data (gen, NULL, NULL);
	yaml_node_unref (root);
	return out != NULL ? out : g_strdup ("{}\n");
}

LrgEnvironment *
lrg_environment_new_from_file (const gchar  *path,
                               GError      **error)
{
	g_autofree gchar         *contents = NULL;
	g_autoptr(LrgEnvironment) self = NULL;
	gsize                     len = 0;
	GError                   *io_error = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &contents, &len, &io_error))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_IO,
		             "Cannot read %s: %s", path, io_error->message);
		g_error_free (io_error);
		return NULL;
	}
	self = lrg_environment_new ();
	if (!lrg_environment_load_from_data (self, contents, (gssize)len, error))
		return NULL;
	return g_steal_pointer (&self);
}

gboolean
lrg_environment_save_to_file (LrgEnvironment  *self,
                              const gchar     *path,
                              GError         **error)
{
	g_autofree gchar *data = NULL;
	GError           *io_error = NULL;

	g_return_val_if_fail (LRG_IS_ENVIRONMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	data = lrg_environment_to_data (self);
	if (!g_file_set_contents (path, data, -1, &io_error))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_IO,
		             "Cannot write %s: %s", path, io_error->message);
		g_error_free (io_error);
		return FALSE;
	}
	return TRUE;
}
