/* lrg-texture-upload.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Uploading block-compressed textures on any GPU.  Data authored for
 * desktop GPUs is usually S3TC (DXT1/3/5), which most OpenGL ES GPUs
 * (Android) cannot sample.  These helpers upload the blocks as they are
 * when the GPU takes them (lrg-gpu-caps.h) and otherwise decode them on
 * the CPU (lrg-texture-codec.h) and upload the fallback format the
 * caller or the process default chooses, every mip level kept:
 *
 *   rgba8     exact colours, 4 bytes per pixel
 *   rgb565    2 bytes per pixel, no alpha (opaque DXT1 data)
 *   rgba4444  2 bytes per pixel, 16 alpha levels (DXT3/DXT5 data)
 *   rgba5551  2 bytes per pixel, 1-bit alpha (DXT1 with cut-outs)
 *
 * libregnum's own texture loads (LrgAssetManager, LrgMaterial) go
 * through lrg_texture_new_from_file(); a game that uploads raw blocks
 * itself (rlLoadTexture() with a DXT format) calls
 * lrg_texture_upload_compressed() or lrg_texture_new_compressed()
 * instead.  The process default comes from
 * lrg_texture_set_default_fallback(), or the environment variable
 * `LRG_TEXTURE_FALLBACK` (`rgba8`, `rgb565`, `rgba4444`, `rgba5551`),
 * read once; it starts as rgba8.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * lrg_texture_get_default_fallback:
 *
 * The fallback %LRG_TEXTURE_FALLBACK_DEFAULT stands for: the one set
 * with lrg_texture_set_default_fallback(), else `LRG_TEXTURE_FALLBACK`
 * from the environment, else %LRG_TEXTURE_FALLBACK_RGBA8.  Never
 * %LRG_TEXTURE_FALLBACK_DEFAULT.
 *
 * Returns: the process default
 */
LRG_AVAILABLE_IN_ALL
LrgTextureFallbackFormat
lrg_texture_get_default_fallback (void);

/**
 * lrg_texture_set_default_fallback:
 * @fallback: the format to decode to, or %LRG_TEXTURE_FALLBACK_DEFAULT
 *   to return to the environment's (or rgba8)
 *
 * Sets the process default (thread safe), for example from a memory
 * setting: rgb565 or rgba4444 halve the memory of a decoded texture.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_texture_set_default_fallback (LrgTextureFallbackFormat fallback);

/**
 * lrg_texture_fallback_get_pixel_format:
 * @fallback: a fallback format; %LRG_TEXTURE_FALLBACK_DEFAULT is resolved
 *   with lrg_texture_get_default_fallback()
 *
 * Returns: the uncompressed #GrlPixelFormat @fallback uploads as
 */
LRG_AVAILABLE_IN_ALL
GrlPixelFormat
lrg_texture_fallback_get_pixel_format (LrgTextureFallbackFormat fallback);

/**
 * lrg_texture_choose_upload_format:
 * @format: the format of the data
 * @fallback: the fallback to use when the GPU lacks @format
 *
 * The decision the upload helpers make: @format itself when the GPU
 * samples it (lrg_gpu_caps_supports_pixel_format()), the fallback's
 * pixel format when it does not and @format is S3TC (which the CPU
 * decodes), and @format again otherwise (ETC2, ASTC and the rest have
 * no CPU decoder; uploading them fails with
 * %LRG_TEXTURE_ERROR_UNSUPPORTED).  No GL calls; the capability
 * overrides steer it.
 *
 * Returns: the format the texture is uploaded in
 */
LRG_AVAILABLE_IN_ALL
GrlPixelFormat
lrg_texture_choose_upload_format (GrlPixelFormat           format,
                                  LrgTextureFallbackFormat fallback);

/**
 * lrg_texture_prepare_upload:
 * @data: (array length=size): the mip chain in @format
 * @size: bytes in @data; more than needed is fine
 * @width: base width in pixels
 * @height: base height in pixels
 * @format: the format of @data
 * @mipmaps: levels in @data, at least 1
 * @fallback: the fallback to use when the GPU lacks @format
 * @out_data: (out) (transfer full): the bytes to upload (a copy of
 *   @data's chain when no conversion is needed)
 * @out_format: (out) (optional): their format
 * @error: return location for a #GError
 *
 * The CPU half of lrg_texture_upload_compressed(): validates the chain,
 * picks the format with lrg_texture_choose_upload_format() and decodes
 * and converts when the fallback is taken.  Needs no GL context.
 *
 * Returns: %TRUE on success; %FALSE with %LRG_TEXTURE_ERROR_INVALID,
 *   _TRUNCATED or _UNSUPPORTED
 */
LRG_AVAILABLE_IN_ALL
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
                            GError                   **error);

/**
 * lrg_texture_upload_compressed:
 * @data: (array length=size): the mip chain in @format
 * @size: bytes in @data; more than needed is fine
 * @width: base width in pixels
 * @height: base height in pixels
 * @format: the format of @data (any format lrg_texture_get_data_size()
 *   knows; compressed or not)
 * @mipmaps: levels in @data, at least 1
 * @fallback: the fallback to use when the GPU lacks @format
 * @out_format: (out) (optional): the format actually uploaded
 * @error: return location for a #GError
 *
 * Uploads a mip chain with rlgl, the drop-in for rlLoadTexture() on
 * data that may be S3TC: the blocks go up as they are when the GPU
 * samples them, else decoded to @fallback.  Needs a current window.
 * The caller owns the returned texture and frees it with
 * rlUnloadTexture().
 *
 * Returns: the GL texture id, or 0 with %LRG_TEXTURE_ERROR_INVALID,
 *   _TRUNCATED, _UNSUPPORTED or _UPLOAD (no window, or GL refused it)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_texture_upload_compressed (const guint8              *data,
                               gsize                      size,
                               guint                      width,
                               guint                      height,
                               GrlPixelFormat             format,
                               guint                      mipmaps,
                               LrgTextureFallbackFormat   fallback,
                               GrlPixelFormat            *out_format,
                               GError                   **error);

/**
 * lrg_texture_new_compressed:
 * @data: (array length=size): the mip chain in @format
 * @size: bytes in @data; more than needed is fine
 * @width: base width in pixels
 * @height: base height in pixels
 * @format: the format of @data
 * @mipmaps: levels in @data, at least 1
 * @fallback: the fallback to use when the GPU lacks @format
 * @error: return location for a #GError
 *
 * lrg_texture_upload_compressed() as a #GrlTexture that owns the GPU
 * texture (grl_texture_get_format() tells which format went up).  Needs
 * a current window; release the texture before the window closes.
 *
 * Returns: (transfer full) (nullable): the texture, or %NULL with an
 *   %LRG_TEXTURE_ERROR
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_texture_new_compressed (const guint8              *data,
                            gsize                      size,
                            guint                      width,
                            guint                      height,
                            GrlPixelFormat             format,
                            guint                      mipmaps,
                            LrgTextureFallbackFormat   fallback,
                            GError                   **error);

/**
 * lrg_texture_new_from_file:
 * @path: an image file
 * @fallback: the fallback for a compressed file the GPU cannot sample
 * @error: return location for a #GError
 *
 * grl_texture_new_from_file() with the compressed fallback: a `.dds`
 * file is read with its mip chain and, when its format is S3TC and the
 * GPU lacks it, decoded to @fallback before the upload.  Other files
 * load exactly as grl_texture_new_from_file() loads them.  Needs a
 * current window.
 *
 * Returns: (transfer full) (nullable): the texture, or %NULL with
 *   %LRG_TEXTURE_ERROR_UPLOAD (unreadable file, failed upload) or
 *   another %LRG_TEXTURE_ERROR
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_texture_new_from_file (const gchar               *path,
                           LrgTextureFallbackFormat   fallback,
                           GError                   **error);

G_END_DECLS
