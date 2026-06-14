/* lrg-glyph-key.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cache key identifying a single rasterised glyph in a #LrgGlyphAtlas.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_GLYPH_KEY (lrg_glyph_key_get_type ())

/**
 * LrgGlyphKey:
 *
 * Identifies one rasterised glyph in a #LrgGlyphAtlas.
 *
 * The key is the tuple (@font_id, @glyph_code, @flags). @font_id is an opaque
 * caller-chosen identity for a font instance (size/hinting/subpixel baked in --
 * the cmacs font bridge uses the address of the Emacs `struct font`). @flags is
 * reserved for synthetic variants (bold/italic/subpixel position) and is 0 today.
 *
 * Since: 1.0
 */
typedef struct _LrgGlyphKey LrgGlyphKey;

LRG_AVAILABLE_IN_ALL
GType lrg_glyph_key_get_type (void) G_GNUC_CONST;

/**
 * lrg_glyph_key_new:
 * @font_id: opaque font-instance identity
 * @glyph_code: backend glyph index within that font
 * @flags: reserved variant flags (0 today)
 *
 * Returns: (transfer full): a new #LrgGlyphKey
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGlyphKey * lrg_glyph_key_new (guint64 font_id,
                                 guint32 glyph_code,
                                 guint32 flags);

LRG_AVAILABLE_IN_ALL
LrgGlyphKey * lrg_glyph_key_copy (const LrgGlyphKey *self);

LRG_AVAILABLE_IN_ALL
void lrg_glyph_key_free (LrgGlyphKey *self);

LRG_AVAILABLE_IN_ALL
guint64 lrg_glyph_key_get_font_id (const LrgGlyphKey *self);

LRG_AVAILABLE_IN_ALL
guint32 lrg_glyph_key_get_glyph_code (const LrgGlyphKey *self);

LRG_AVAILABLE_IN_ALL
guint32 lrg_glyph_key_get_flags (const LrgGlyphKey *self);

/**
 * lrg_glyph_key_hash:
 * @key: (type LrgGlyphKey): a #LrgGlyphKey
 *
 * A #GHashFunc over the key tuple.
 *
 * Returns: a hash value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_glyph_key_hash (gconstpointer key);

/**
 * lrg_glyph_key_equal:
 * @a: (type LrgGlyphKey): a #LrgGlyphKey
 * @b: (type LrgGlyphKey): another #LrgGlyphKey
 *
 * A #GEqualFunc over the key tuple.
 *
 * Returns: %TRUE if the keys are equal
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_glyph_key_equal (gconstpointer a,
                              gconstpointer b);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgGlyphKey, lrg_glyph_key_free)

G_END_DECLS
