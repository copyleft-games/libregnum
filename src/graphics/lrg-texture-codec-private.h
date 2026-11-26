/* lrg-texture-codec-private.h - shared S3TC block decoding
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The BC1 .. BC3 level decoder behind lrg_texture_decode() and
 * LrgDdsImage, so both read blocks the same way.  Callers have already
 * proved @src holds every block of the level.
 *
 * Internal header; not installed.
 */

#pragma once

#include <glib.h>

/*
 * LrgBcKind:
 * @LRG_BC_KIND_BC1_RGB: BC1 for an opaque format (DXT1 RGB): the
 *   three-colour block's fourth entry is opaque black
 * @LRG_BC_KIND_BC1_RGBA: BC1 with 1-bit alpha (DXT1 RGBA): that entry
 *   is transparent black
 * @LRG_BC_KIND_BC2: BC2 (DXT3), explicit 4-bit alpha
 * @LRG_BC_KIND_BC3: BC3 (DXT5), interpolated alpha
 */
typedef enum
{
	LRG_BC_KIND_BC1_RGB,
	LRG_BC_KIND_BC1_RGBA,
	LRG_BC_KIND_BC2,
	LRG_BC_KIND_BC3
} LrgBcKind;

/*
 * lrg_texture_bc_block_size:
 * @kind: the block kind
 *
 * Returns: the bytes per 4x4 block (8 for BC1, 16 otherwise)
 */
G_GNUC_INTERNAL
guint
lrg_texture_bc_block_size (LrgBcKind kind);

/*
 * lrg_texture_decode_bc_level:
 * @kind: the block kind
 * @src: ((width + 3) / 4) * ((height + 3) / 4) blocks, row by row
 * @width: level width in pixels (> 0)
 * @height: level height in pixels (> 0)
 * @dst: width * height * 4 bytes of RGBA8 output
 *
 * Decodes one level; blocks overhanging a non-multiple-of-4 edge are
 * clipped.
 */
G_GNUC_INTERNAL
void
lrg_texture_decode_bc_level (LrgBcKind     kind,
                             const guint8 *src,
                             guint         width,
                             guint         height,
                             guint8       *dst);
