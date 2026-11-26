/* lrg-gpu-caps.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * What the GPU can sample: the compressed texture families the current
 * GL context accepts.  Desktop GPUs take S3TC (DXT) and rarely ASTC;
 * OpenGL ES 3 GPUs (Android) take ETC2 always and ASTC often, S3TC
 * seldom.  The answer comes from rlgl, which parses the context's
 * extension strings (GL_EXT_texture_compression_s3tc, the WebGL
 * variants, GL_ARB_ES3_compatibility / ES 3 core for ETC2,
 * GL_KHR_texture_compression_astc_*) when the window opens.
 *
 * Overrides force an answer for tests and for trying a phone's
 * fallback path on a desktop: lrg_gpu_caps_set_override() in code, or
 * the environment variable `LRG_GPU_DISABLE_TEXTURE_COMPRESSION`, a
 * comma-separated list of `s3tc`, `etc2`, `astc` or `all`, read once on
 * the first query.
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
 * lrg_gpu_caps_has_texture_compression:
 * @family: a compressed texture family
 *
 * Whether the GPU samples @family.  An override
 * (lrg_gpu_caps_set_override() or the environment) answers first;
 * otherwise the current GL context is asked through rlgl.  Without a
 * window (headless, before the window opens) nothing is known and the
 * answer is %FALSE.
 *
 * Returns: %TRUE when textures of @family can be uploaded as they are
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gpu_caps_has_texture_compression (LrgTextureCompression family);

/**
 * lrg_gpu_caps_supports_pixel_format:
 * @format: a pixel format
 *
 * Whether a texture in @format uploads as it is: the family's answer
 * for a compressed format (DXT1/3/5 are %LRG_TEXTURE_COMPRESSION_S3TC,
 * ETC2 RGB and EAC RGBA %LRG_TEXTURE_COMPRESSION_ETC2, ASTC 4x4 and 8x8
 * %LRG_TEXTURE_COMPRESSION_ASTC; ETC1 and PVRTC are asked of the
 * context directly), %TRUE for 8-bit and 16-bit uncompressed formats.
 *
 * Returns: %TRUE when @format needs no fallback
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gpu_caps_supports_pixel_format (GrlPixelFormat format);

/**
 * lrg_gpu_caps_get_texture_compression_family:
 * @format: a pixel format
 * @out_family: (out) (optional): the family of @format
 *
 * Which #LrgTextureCompression family @format belongs to.
 *
 * Returns: %TRUE when @format is in one of the families
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gpu_caps_get_texture_compression_family (GrlPixelFormat         format,
                                             LrgTextureCompression *out_family);

/**
 * lrg_gpu_caps_set_override:
 * @family: a compressed texture family
 * @value: %LRG_GPU_CAP_OVERRIDE_AUTO to ask the context again,
 *   %LRG_GPU_CAP_OVERRIDE_SUPPORTED or %LRG_GPU_CAP_OVERRIDE_UNSUPPORTED
 *   to force the answer
 *
 * Forces what lrg_gpu_caps_has_texture_compression() answers for
 * @family (process wide, thread safe; it takes precedence over the
 * environment).  Forcing "supported" on a GPU without the family makes
 * uploads of it fail; forcing "unsupported" makes the upload helpers
 * take their CPU fallback, the way tests and desktop runs exercise the
 * Android path.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gpu_caps_set_override (LrgTextureCompression family,
                           LrgGpuCapOverride     value);

/**
 * lrg_gpu_caps_get_override:
 * @family: a compressed texture family
 *
 * The override in effect for @family: the one set with
 * lrg_gpu_caps_set_override(), else %LRG_GPU_CAP_OVERRIDE_UNSUPPORTED
 * when `LRG_GPU_DISABLE_TEXTURE_COMPRESSION` names it, else
 * %LRG_GPU_CAP_OVERRIDE_AUTO.
 *
 * Returns: the override
 */
LRG_AVAILABLE_IN_ALL
LrgGpuCapOverride
lrg_gpu_caps_get_override (LrgTextureCompression family);

/**
 * lrg_gpu_caps_reset_overrides:
 *
 * Drops every override set with lrg_gpu_caps_set_override() (the
 * environment still counts).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gpu_caps_reset_overrides (void);

G_END_DECLS
