/* lrg-meshopt.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Decoding of the EXT_meshopt_compression glTF extension's buffer views:
 * the attribute (vertex) codec, the triangle and index-sequence codecs and
 * the octahedral, quaternion and exponential filters.  Written in C from
 * the extension's specification and the reference decoder that ships with
 * meshoptimizer (MIT License, Copyright (C) 2016-2024 Arseny Kapoulkine;
 * reference decoder by Jasper St. Pierre); no code of it is vendored.
 * Needs no graphics context.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgMeshoptMode:
 * @LRG_MESHOPT_MODE_ATTRIBUTES: vertex attributes (header 0xa0)
 * @LRG_MESHOPT_MODE_TRIANGLES: a triangle list's indices (header 0xe1)
 * @LRG_MESHOPT_MODE_INDICES: any other index sequence (header 0xd1)
 *
 * The extension's "mode" of a compressed buffer view.
 */
typedef enum
{
    LRG_MESHOPT_MODE_ATTRIBUTES = 0,
    LRG_MESHOPT_MODE_TRIANGLES  = 1,
    LRG_MESHOPT_MODE_INDICES    = 2
} LrgMeshoptMode;

/**
 * LrgMeshoptFilter:
 * @LRG_MESHOPT_FILTER_NONE: no filter
 * @LRG_MESHOPT_FILTER_OCTAHEDRAL: octahedral normals and tangents
 *   (stride 4 or 8)
 * @LRG_MESHOPT_FILTER_QUATERNION: rotations as the three smallest
 *   components (stride 8)
 * @LRG_MESHOPT_FILTER_EXPONENTIAL: floats as an 8-bit exponent and a
 *   24-bit mantissa (stride a multiple of 4)
 *
 * The extension's "filter" applied after the attribute codec.
 */
typedef enum
{
    LRG_MESHOPT_FILTER_NONE        = 0,
    LRG_MESHOPT_FILTER_OCTAHEDRAL  = 1,
    LRG_MESHOPT_FILTER_QUATERNION  = 2,
    LRG_MESHOPT_FILTER_EXPONENTIAL = 3
} LrgMeshoptFilter;

/**
 * lrg_meshopt_decode:
 * @mode: the view's codec
 * @filter: the view's filter (only with %LRG_MESHOPT_MODE_ATTRIBUTES)
 * @source: (array length=source_size): the compressed bytes
 * @source_size: their count (the extension's byteLength)
 * @count: element count
 * @stride: element size in bytes (a multiple of 4 up to 256 for
 *   attributes; 2 or 4 for indices)
 * @error: return location for a #GError, or %NULL
 *
 * Decodes one EXT_meshopt_compression buffer view.  Malformed input
 * (a wrong header, a stream that ends early or does not end where its
 * tail begins, a bad stride or count) is %LRG_GLTF_ERROR_INVALID_DATA;
 * nothing is read past @source_size.
 *
 * Returns: (transfer full) (nullable): @count x @stride bytes, little
 *   endian as glTF stores them, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_meshopt_decode (LrgMeshoptMode     mode,
                    LrgMeshoptFilter   filter,
                    const guint8      *source,
                    gsize              source_size,
                    guint              count,
                    guint              stride,
                    GError           **error);

G_END_DECLS
