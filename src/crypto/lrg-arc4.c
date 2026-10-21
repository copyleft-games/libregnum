/* lrg-arc4.c - The ARC4 (RC4) stream cipher
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The textbook algorithm: the key-scheduling algorithm (KSA) permutes
 * S = 0..255 under the key, and the pseudo-random generation algorithm
 * (PRGA) emits one keystream byte per step from (i, j, S).  Verified
 * against the RFC 6229 vectors in tests/test-crypto.c.
 */

#include "config.h"
#include "lrg-arc4.h"

#include <string.h>

#include <openssl/crypto.h>

struct _LrgArc4
{
    guint8 s[256];
    guint8 i;
    guint8 j;
};

G_DEFINE_BOXED_TYPE (LrgArc4, lrg_arc4, lrg_arc4_copy, lrg_arc4_free)

/*
 * next_byte:
 * One PRGA step: advance i, mix S[i] into j, swap, and return the
 * byte S[S[i] + S[j]].  guint8 arithmetic gives the mod 256 for free.
 */
static inline guint8
next_byte (LrgArc4 *self)
{
    guint8 t;

    self->i = (guint8) (self->i + 1);
    self->j = (guint8) (self->j + self->s[self->i]);
    t = self->s[self->i];
    self->s[self->i] = self->s[self->j];
    self->s[self->j] = t;
    return self->s[(guint8) (self->s[self->i] + self->s[self->j])];
}

LrgArc4 *
lrg_arc4_new (GBytes *key)
{
    const guint8 *k;
    LrgArc4 *self;
    gsize key_len;
    guint8 j;
    guint8 t;
    guint n;

    g_return_val_if_fail (key != NULL, NULL);
    k = g_bytes_get_data (key, &key_len);
    g_return_val_if_fail (key_len >= 1 && key_len <= 256, NULL);

    /* KSA: identity permutation, then 256 key-driven swaps. */
    self = g_new0 (LrgArc4, 1);
    for (n = 0; n < 256; n++)
        self->s[n] = (guint8) n;
    j = 0;
    for (n = 0; n < 256; n++)
    {
        j = (guint8) (j + self->s[n] + k[n % key_len]);
        t = self->s[n];
        self->s[n] = self->s[j];
        self->s[j] = t;
    }
    self->i = 0;
    self->j = 0;
    return self;
}

LrgArc4 *
lrg_arc4_new_drop (GBytes *key,
                   gsize   n_drop)
{
    LrgArc4 *self;

    self = lrg_arc4_new (key);
    if (self != NULL)
        lrg_arc4_discard (self, n_drop);
    return self;
}

LrgArc4 *
lrg_arc4_copy (const LrgArc4 *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return g_memdup2 (self, sizeof *self);
}

void
lrg_arc4_free (LrgArc4 *self)
{
    if (self == NULL)
        return;
    /* The permutation is equivalent to the key; wipe it. */
    OPENSSL_cleanse (self, sizeof *self);
    g_free (self);
}

void
lrg_arc4_discard (LrgArc4 *self,
                  gsize    n_bytes)
{
    gsize n;

    g_return_if_fail (self != NULL);

    for (n = 0; n < n_bytes; n++)
        (void) next_byte (self);
}

void
lrg_arc4_process (LrgArc4 *self,
                  guint8  *data,
                  gsize    length)
{
    gsize n;

    g_return_if_fail (self != NULL);
    g_return_if_fail (data != NULL || length == 0);

    for (n = 0; n < length; n++)
        data[n] ^= next_byte (self);
}

GBytes *
lrg_arc4_process_bytes (LrgArc4 *self,
                        GBytes  *input)
{
    guint8 *out;
    gsize length;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (input != NULL, NULL);

    out = g_bytes_unref_to_data (g_bytes_ref (input), &length);
    lrg_arc4_process (self, out, length);
    return g_bytes_new_take (out, length);
}
