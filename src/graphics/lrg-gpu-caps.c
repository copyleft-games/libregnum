/* lrg-gpu-caps.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compressed texture capabilities.  rlgl keeps its extension flags
 * private, but rlGetGlTextureFormats() reports a GL internal format of
 * 0 for a compressed format the context lacks, so asking it for one
 * representative format per family reads the same flags without
 * touching GL.  Overrides are atomics; the environment is parsed once.
 */

#include "config.h"

#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-gpu-caps.h"

#define N_FAMILIES (3)

/* the lrg_gpu_caps_set_override() values, LRG_GPU_CAP_OVERRIDE_AUTO = 0 */
static gint overrides[N_FAMILIES];

/* bit i set: LRG_GPU_DISABLE_TEXTURE_COMPRESSION names family i */
static gsize env_disabled_init;
static guint env_disabled;

static gboolean
family_is_valid (LrgTextureCompression family)
{
	return family == LRG_TEXTURE_COMPRESSION_S3TC || family == LRG_TEXTURE_COMPRESSION_ETC2 ||
	       family == LRG_TEXTURE_COMPRESSION_ASTC;
}

/* the families the environment disables, parsed once */
static guint
get_env_disabled (void)
{
	if (g_once_init_enter (&env_disabled_init))
	{
		const gchar *value = g_getenv ("LRG_GPU_DISABLE_TEXTURE_COMPRESSION");
		g_auto(GStrv) names = NULL;
		guint mask = 0;
		guint i;

		if (value != NULL)
			names = g_strsplit (value, ",", -1);
		for (i = 0; names != NULL && names[i] != NULL; i++)
		{
			g_autofree gchar *name = g_ascii_strdown (g_strstrip (names[i]), -1);

			if (g_str_equal (name, "s3tc") || g_str_equal (name, "dxt"))
				mask |= 1u << LRG_TEXTURE_COMPRESSION_S3TC;
			else if (g_str_equal (name, "etc2"))
				mask |= 1u << LRG_TEXTURE_COMPRESSION_ETC2;
			else if (g_str_equal (name, "astc"))
				mask |= 1u << LRG_TEXTURE_COMPRESSION_ASTC;
			else if (g_str_equal (name, "all"))
				mask |= (1u << N_FAMILIES) - 1;
			else if (name[0] != '\0')
				g_debug ("LRG_GPU_DISABLE_TEXTURE_COMPRESSION: unknown family '%s'", name);
		}
		env_disabled = mask;
		g_once_init_leave (&env_disabled_init, 1);
	}
	return env_disabled;
}

/* whether the current context has a GL internal format for @format */
static gboolean
context_supports (GrlPixelFormat format)
{
	unsigned int internal_format;
	unsigned int gl_format;
	unsigned int gl_type;

	/* before the window (or headless) rlgl has parsed no extensions */
	if (!IsWindowReady ())
		return FALSE;
	rlGetGlTextureFormats ((int)format, &internal_format, &gl_format, &gl_type);
	return internal_format != 0;
}

LrgGpuCapOverride
lrg_gpu_caps_get_override (LrgTextureCompression family)
{
	gint value;

	g_return_val_if_fail (family_is_valid (family), LRG_GPU_CAP_OVERRIDE_AUTO);

	value = g_atomic_int_get (&overrides[family]);
	if (value != LRG_GPU_CAP_OVERRIDE_AUTO)
		return (LrgGpuCapOverride)value;
	if ((get_env_disabled () & (1u << family)) != 0)
		return LRG_GPU_CAP_OVERRIDE_UNSUPPORTED;
	return LRG_GPU_CAP_OVERRIDE_AUTO;
}

void
lrg_gpu_caps_set_override (LrgTextureCompression family,
                           LrgGpuCapOverride     value)
{
	g_return_if_fail (family_is_valid (family));
	g_return_if_fail (value == LRG_GPU_CAP_OVERRIDE_AUTO || value == LRG_GPU_CAP_OVERRIDE_SUPPORTED ||
	                  value == LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);

	g_atomic_int_set (&overrides[family], (gint)value);
}

void
lrg_gpu_caps_reset_overrides (void)
{
	guint i;

	for (i = 0; i < N_FAMILIES; i++)
		g_atomic_int_set (&overrides[i], LRG_GPU_CAP_OVERRIDE_AUTO);
}

gboolean
lrg_gpu_caps_has_texture_compression (LrgTextureCompression family)
{
	g_return_val_if_fail (family_is_valid (family), FALSE);

	switch (lrg_gpu_caps_get_override (family))
	{
	case LRG_GPU_CAP_OVERRIDE_SUPPORTED:
		return TRUE;
	case LRG_GPU_CAP_OVERRIDE_UNSUPPORTED:
		return FALSE;
	case LRG_GPU_CAP_OVERRIDE_AUTO:
	default:
		break;
	}

	/* one format stands for its family: rlgl sets a single flag each */
	switch (family)
	{
	case LRG_TEXTURE_COMPRESSION_S3TC:
		return context_supports (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA);
	case LRG_TEXTURE_COMPRESSION_ETC2:
		return context_supports (GRL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA);
	case LRG_TEXTURE_COMPRESSION_ASTC:
	default:
		return context_supports (GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA);
	}
}

gboolean
lrg_gpu_caps_get_texture_compression_family (GrlPixelFormat         format,
                                             LrgTextureCompression *out_family)
{
	LrgTextureCompression family;

	switch (format)
	{
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:
		family = LRG_TEXTURE_COMPRESSION_S3TC;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:
		family = LRG_TEXTURE_COMPRESSION_ETC2;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:
		family = LRG_TEXTURE_COMPRESSION_ASTC;
		break;
	default:
		return FALSE;
	}
	if (out_family != NULL)
		*out_family = family;
	return TRUE;
}

gboolean
lrg_gpu_caps_supports_pixel_format (GrlPixelFormat format)
{
	LrgTextureCompression family;

	if (lrg_gpu_caps_get_texture_compression_family (format, &family))
		return lrg_gpu_caps_has_texture_compression (family);

	switch (format)
	{
	case GRL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGBA:
		return context_supports (format);
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
		return TRUE;
	default:
		/* floating point: whatever the context says */
		return context_supports (format);
	}
}
