/* lrg-texture-sampling.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * raylib's SetTextureFilter() knows point, bilinear and trilinear only;
 * mipmapped point sampling needs GL_NEAREST_MIPMAP_LINEAR, set here
 * through rlgl after graylib generated the mipmaps.
 */

#include "config.h"

#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-texture-sampling.h"

gboolean
lrg_texture_sampling_uses_mipmaps (LrgTextureSampling sampling)
{
    return sampling == LRG_TEXTURE_SAMPLING_POINT_MIPMAPPED
        || sampling == LRG_TEXTURE_SAMPLING_TRILINEAR;
}

gboolean
lrg_texture_set_sampling (GrlTexture         *texture,
                          LrgTextureSampling  sampling)
{
    Texture2D *handle;
    gint       min_filter;
    gint       mag_filter;

    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);

    if (!IsWindowReady ())
        return FALSE;
    handle = (Texture2D *)grl_texture_get_handle (texture);
    if (handle == NULL || handle->id == 0)
        return FALSE;

    /* 1. the mipmap chain, once (a texture keeps its levels) */
    if (lrg_texture_sampling_uses_mipmaps (sampling) && grl_texture_get_mipmaps (texture) <= 1)
        grl_texture_gen_mipmaps (texture);

    /* 2. the filters */
    switch (sampling)
    {
    case LRG_TEXTURE_SAMPLING_POINT_MIPMAPPED:
        min_filter = RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR;
        mag_filter = RL_TEXTURE_FILTER_NEAREST;
        break;
    case LRG_TEXTURE_SAMPLING_BILINEAR:
        min_filter = RL_TEXTURE_FILTER_LINEAR;
        mag_filter = RL_TEXTURE_FILTER_LINEAR;
        break;
    case LRG_TEXTURE_SAMPLING_TRILINEAR:
        min_filter = RL_TEXTURE_FILTER_MIP_LINEAR;
        mag_filter = RL_TEXTURE_FILTER_LINEAR;
        break;
    case LRG_TEXTURE_SAMPLING_POINT:
    default:
        min_filter = RL_TEXTURE_FILTER_NEAREST;
        mag_filter = RL_TEXTURE_FILTER_NEAREST;
        break;
    }
    rlTextureParameters (handle->id, RL_TEXTURE_MIN_FILTER, min_filter);
    rlTextureParameters (handle->id, RL_TEXTURE_MAG_FILTER, mag_filter);
    return TRUE;
}
