/* lrg-texture-upload.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compressed texture uploads with a CPU fallback.  The decision and the
 * conversion (prepare) are pure and run without GL; the upload hands
 * the result to rlgl's rlLoadTexture(), which uploads every level of a
 * chain in any raylib format.  A GrlTexture that owns the upload is made
 * through grl_texture_new_from_image() on a GrlImage whose raylib Image
 * this file fills with the chain (graylib has no constructor for raw
 * levels in an arbitrary format).
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-texture-upload.h"
#include "graphics/lrg-texture-codec.h"
#include "graphics/lrg-gpu-caps.h"

/* the lrg_texture_set_default_fallback() value; DEFAULT = not set */
static gint default_fallback = LRG_TEXTURE_FALLBACK_DEFAULT;

/* LRG_TEXTURE_FALLBACK, parsed once (DEFAULT when unset or unknown) */
static gsize env_fallback_init;
static gint env_fallback;

static gboolean
fallback_is_valid (LrgTextureFallbackFormat fallback)
{
	return fallback >= LRG_TEXTURE_FALLBACK_DEFAULT && fallback <= LRG_TEXTURE_FALLBACK_RGBA5551;
}

static LrgTextureFallbackFormat
get_env_fallback (void)
{
	if (g_once_init_enter (&env_fallback_init))
	{
		const gchar *value = g_getenv ("LRG_TEXTURE_FALLBACK");
		GEnumClass *klass;
		GEnumValue *found = NULL;

		env_fallback = LRG_TEXTURE_FALLBACK_DEFAULT;
		if (value != NULL && value[0] != '\0')
		{
			klass = g_type_class_ref (LRG_TYPE_TEXTURE_FALLBACK_FORMAT);
			found = g_enum_get_value_by_nick (klass, value);
			if (found != NULL)
				env_fallback = found->value;
			else
				g_debug ("LRG_TEXTURE_FALLBACK: unknown format '%s'", value);
			g_type_class_unref (klass);
		}
		g_once_init_leave (&env_fallback_init, 1);
	}
	return (LrgTextureFallbackFormat)env_fallback;
}

LrgTextureFallbackFormat
lrg_texture_get_default_fallback (void)
{
	LrgTextureFallbackFormat fallback;

	fallback = (LrgTextureFallbackFormat)g_atomic_int_get (&default_fallback);
	if (fallback != LRG_TEXTURE_FALLBACK_DEFAULT)
		return fallback;
	fallback = get_env_fallback ();
	if (fallback != LRG_TEXTURE_FALLBACK_DEFAULT)
		return fallback;
	return LRG_TEXTURE_FALLBACK_RGBA8;
}

void
lrg_texture_set_default_fallback (LrgTextureFallbackFormat fallback)
{
	g_return_if_fail (fallback_is_valid (fallback));

	g_atomic_int_set (&default_fallback, (gint)fallback);
}

GrlPixelFormat
lrg_texture_fallback_get_pixel_format (LrgTextureFallbackFormat fallback)
{
	g_return_val_if_fail (fallback_is_valid (fallback), GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

	if (fallback == LRG_TEXTURE_FALLBACK_DEFAULT)
		fallback = lrg_texture_get_default_fallback ();
	switch (fallback)
	{
	case LRG_TEXTURE_FALLBACK_RGB565:
		return GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5;
	case LRG_TEXTURE_FALLBACK_RGBA4444:
		return GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4;
	case LRG_TEXTURE_FALLBACK_RGBA5551:
		return GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1;
	case LRG_TEXTURE_FALLBACK_RGBA8:
	case LRG_TEXTURE_FALLBACK_DEFAULT:
	default:
		return GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	}
}

GrlPixelFormat
lrg_texture_choose_upload_format (GrlPixelFormat           format,
                                  LrgTextureFallbackFormat fallback)
{
	if (lrg_gpu_caps_supports_pixel_format (format) || !lrg_texture_format_is_s3tc (format))
		return format;
	return lrg_texture_fallback_get_pixel_format (fallback);
}

/*
 * prepare:
 * The shared CPU half.  On success *@out_ptr is what to upload and
 * *@out_size its byte count; *@out_owned holds the converted bytes when
 * a conversion ran (NULL when *@out_ptr points into @data, which is
 * then valid for exactly the chain).
 */
static gboolean
prepare (const guint8              *data,
         gsize                      size,
         guint                      width,
         guint                      height,
         GrlPixelFormat             format,
         guint                      mipmaps,
         LrgTextureFallbackFormat   fallback,
         const guint8             **out_ptr,
         gsize                     *out_size,
         GBytes                   **out_owned,
         GrlPixelFormat            *out_format,
         GError                   **error)
{
	g_autoptr(GBytes) rgba = NULL;
	GrlPixelFormat target;
	LrgTextureCompression family;
	gsize needed;

	*out_owned = NULL;
	if (!fallback_is_valid (fallback))
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID,
		             "upload: unknown fallback format %d", (gint)fallback);
		return FALSE;
	}

	/* 1. the chain must be whole: same checks as the codecs */
	if (width == 0 || height == 0 || width > LRG_TEXTURE_MAX_SIZE || height > LRG_TEXTURE_MAX_SIZE ||
	    mipmaps == 0 || mipmaps > lrg_texture_get_max_mipmaps (width, height))
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID,
		             "upload: impossible texture %u x %u with %u levels", width, height, mipmaps);
		return FALSE;
	}
	needed = lrg_texture_get_data_size (format, width, height, mipmaps);
	if (needed == 0)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED,
		             "upload: pixel format %d has no known size", (gint)format);
		return FALSE;
	}
	if (data == NULL || size < needed)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED,
		             "upload: %u x %u with %u levels needs %" G_GSIZE_FORMAT " bytes, got %"
		             G_GSIZE_FORMAT, width, height, mipmaps, needed, data == NULL ? (gsize)0 : size);
		return FALSE;
	}

	/* 2. as it is, when the GPU samples it */
	target = lrg_texture_choose_upload_format (format, fallback);
	if (target == format)
	{
		if (!lrg_gpu_caps_supports_pixel_format (format))
		{
			g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED,
			             "upload: the GPU cannot sample pixel format %d (%s) and there is no CPU decoder for it",
			             (gint)format,
			             lrg_gpu_caps_get_texture_compression_family (format, &family)
			                 ? (family == LRG_TEXTURE_COMPRESSION_ETC2 ? "ETC2" : "ASTC")
			                 : "no family");
			return FALSE;
		}
		*out_ptr = data;
		*out_size = needed;
		if (out_format != NULL)
			*out_format = format;
		return TRUE;
	}

	/* 3. the fallback: decode to RGBA8, then pack to the target */
	rgba = lrg_texture_decode (data, size, width, height, format, mipmaps, error);
	if (rgba == NULL)
		return FALSE;
	if (target == GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
	{
		*out_owned = g_steal_pointer (&rgba);
	}
	else
	{
		*out_owned = lrg_texture_convert_rgba8 (g_bytes_get_data (rgba, NULL), g_bytes_get_size (rgba),
		                                        width, height, mipmaps, target, error);
		if (*out_owned == NULL)
			return FALSE;
	}
	*out_ptr = g_bytes_get_data (*out_owned, out_size);
	if (out_format != NULL)
		*out_format = target;
	return TRUE;
}

gboolean
lrg_texture_prepare_upload (const guint8              *data,
                            gsize                      size,
                            guint                      width,
                            guint                      height,
                            GrlPixelFormat             format,
                            guint                      mipmaps,
                            LrgTextureFallbackFormat   fallback,
                            GBytes                   **out_data,
                            GrlPixelFormat            *out_format,
                            GError                   **error)
{
	g_autoptr(GBytes) owned = NULL;
	const guint8 *ptr = NULL;
	gsize ptr_size = 0;

	g_return_val_if_fail (out_data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	*out_data = NULL;
	if (!prepare (data, size, width, height, format, mipmaps, fallback, &ptr, &ptr_size, &owned,
	              out_format, error))
		return FALSE;
	*out_data = owned != NULL ? g_steal_pointer (&owned) : g_bytes_new (ptr, ptr_size);
	return TRUE;
}

guint
lrg_texture_upload_compressed (const guint8              *data,
                               gsize                      size,
                               guint                      width,
                               guint                      height,
                               GrlPixelFormat             format,
                               guint                      mipmaps,
                               LrgTextureFallbackFormat   fallback,
                               GrlPixelFormat            *out_format,
                               GError                   **error)
{
	g_autoptr(GBytes) owned = NULL;
	const guint8 *ptr = NULL;
	gsize ptr_size = 0;
	GrlPixelFormat target = format;
	guint id;

	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	if (!prepare (data, size, width, height, format, mipmaps, fallback, &ptr, &ptr_size, &owned,
	              &target, error))
		return 0;
	if (!IsWindowReady ())
	{
		g_set_error_literal (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		                     "upload: a texture upload needs a current window");
		return 0;
	}

	id = rlLoadTexture (ptr, (gint)width, (gint)height, (gint)target, (gint)mipmaps);
	if (id == 0)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		             "upload: GL refused a %u x %u texture in pixel format %d", width, height,
		             (gint)target);
		return 0;
	}
	if (out_format != NULL)
		*out_format = target;
	return id;
}

GrlTexture *
lrg_texture_new_compressed (const guint8              *data,
                            gsize                      size,
                            guint                      width,
                            guint                      height,
                            GrlPixelFormat             format,
                            guint                      mipmaps,
                            LrgTextureFallbackFormat   fallback,
                            GError                   **error)
{
	static const guint8 placeholder[4] = { 0, 0, 0, 0 };
	g_autoptr(GBytes) owned = NULL;
	g_autoptr(GrlImage) image = NULL;
	g_autoptr(GrlTexture) texture = NULL;
	const guint8 *ptr = NULL;
	gsize ptr_size = 0;
	GrlPixelFormat target = format;
	Image *handle;
	void *copy;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!prepare (data, size, width, height, format, mipmaps, fallback, &ptr, &ptr_size, &owned,
	              &target, error))
		return NULL;
	if (!IsWindowReady ())
	{
		g_set_error_literal (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		                     "upload: a texture upload needs a current window");
		return NULL;
	}

	/* a GrlImage carrying the chain: raylib frees Image.data with free(),
	 * so the copy is malloc'd and the 1x1 placeholder's pixels released
	 * the same way before the swap */
	copy = malloc (ptr_size);
	if (copy == NULL)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		             "upload: out of memory for %" G_GSIZE_FORMAT " bytes", ptr_size);
		return NULL;
	}
	memcpy (copy, ptr, ptr_size);
	g_clear_pointer (&owned, g_bytes_unref);

	image = grl_image_new_from_pixels (1, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, placeholder);
	if (image == NULL)
	{
		free (copy);
		g_set_error_literal (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		                     "upload: could not create the staging image");
		return NULL;
	}
	handle = (Image *)grl_image_get_handle (image);
	free (handle->data);
	handle->data = copy;
	handle->width = (gint)width;
	handle->height = (gint)height;
	handle->mipmaps = (gint)mipmaps;
	handle->format = (gint)target;

	texture = grl_texture_new_from_image (image);
	if (texture == NULL || !grl_texture_is_valid (texture))
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		             "upload: GL refused a %u x %u texture in pixel format %d", width, height,
		             (gint)target);
		return NULL;
	}
	return g_steal_pointer (&texture);
}

GrlTexture *
lrg_texture_new_from_file (const gchar               *path,
                           LrgTextureFallbackFormat   fallback,
                           GError                   **error)
{
	g_autoptr(GrlImage) image = NULL;
	g_autoptr(GrlTexture) texture = NULL;
	g_autofree gchar *lower = NULL;
	GrlPixelFormat format;
	Image *handle;
	gsize size;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* 1. only DDS (the compressed container raylib reads) can need the
	 *    fallback; everything else loads exactly as before */
	lower = g_ascii_strdown (path, -1);
	if (!g_str_has_suffix (lower, ".dds"))
	{
		texture = grl_texture_new_from_file (path);
		if (texture == NULL || !grl_texture_is_valid (texture))
		{
			g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
			             "could not load texture %s", path);
			return NULL;
		}
		return g_steal_pointer (&texture);
	}

	/* 2. read the chain, then upload it as it is or decoded */
	image = grl_image_new_from_file (path);
	if (image == NULL || !grl_image_is_valid (image))
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
		             "could not read texture %s", path);
		return NULL;
	}
	format = grl_image_get_format (image);
	if (!lrg_texture_format_is_compressed (format) || lrg_gpu_caps_supports_pixel_format (format))
	{
		texture = grl_texture_new_from_image (image);
		if (texture == NULL || !grl_texture_is_valid (texture))
		{
			g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD,
			             "could not upload texture %s", path);
			return NULL;
		}
		return g_steal_pointer (&texture);
	}

	handle = (Image *)grl_image_get_handle (image);
	size = lrg_texture_get_data_size (format, (guint)handle->width, (guint)handle->height,
	                                  (guint)MAX (handle->mipmaps, 1));
	texture = lrg_texture_new_compressed (handle->data, size, (guint)handle->width,
	                                      (guint)handle->height, format,
	                                      (guint)MAX (handle->mipmaps, 1), fallback, error);
	if (texture == NULL)
		g_prefix_error (error, "%s: ", path);
	return g_steal_pointer (&texture);
}
