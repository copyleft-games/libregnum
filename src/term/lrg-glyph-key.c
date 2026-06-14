/* lrg-glyph-key.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-glyph-key.h"

struct _LrgGlyphKey
{
    guint64 font_id;
    guint32 glyph_code;
    guint32 flags;
};

G_DEFINE_BOXED_TYPE (LrgGlyphKey, lrg_glyph_key,
                     lrg_glyph_key_copy, lrg_glyph_key_free)

LrgGlyphKey *
lrg_glyph_key_new (guint64 font_id,
                   guint32 glyph_code,
                   guint32 flags)
{
    LrgGlyphKey *self = g_new0 (LrgGlyphKey, 1);

    self->font_id = font_id;
    self->glyph_code = glyph_code;
    self->flags = flags;

    return self;
}

LrgGlyphKey *
lrg_glyph_key_copy (const LrgGlyphKey *self)
{
    if (self == NULL)
        return NULL;

    return lrg_glyph_key_new (self->font_id, self->glyph_code, self->flags);
}

void
lrg_glyph_key_free (LrgGlyphKey *self)
{
    g_free (self);
}

guint64
lrg_glyph_key_get_font_id (const LrgGlyphKey *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->font_id;
}

guint32
lrg_glyph_key_get_glyph_code (const LrgGlyphKey *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->glyph_code;
}

guint32
lrg_glyph_key_get_flags (const LrgGlyphKey *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->flags;
}

guint
lrg_glyph_key_hash (gconstpointer key)
{
    const LrgGlyphKey *self = key;
    guint64 h;

    if (self == NULL)
        return 0;

    /* Mix the three fields into a 64-bit value, then fold to 32. */
    h = self->font_id;
    h = (h * G_GUINT64_CONSTANT (1099511628211)) ^ self->glyph_code;
    h = (h * G_GUINT64_CONSTANT (1099511628211)) ^ self->flags;

    return (guint) (h ^ (h >> 32));
}

gboolean
lrg_glyph_key_equal (gconstpointer a,
                     gconstpointer b)
{
    const LrgGlyphKey *ka = a;
    const LrgGlyphKey *kb = b;

    if (ka == kb)
        return TRUE;
    if (ka == NULL || kb == NULL)
        return FALSE;

    return ka->font_id == kb->font_id
        && ka->glyph_code == kb->glyph_code
        && ka->flags == kb->flags;
}
