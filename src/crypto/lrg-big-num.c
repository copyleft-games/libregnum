/* lrg-big-num.c - Immutable arbitrary-precision non-negative integers
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Each LrgBigNum owns one BIGNUM that is never modified after the
 * constructor returns.  Arithmetic allocates a fresh BIGNUM for the
 * result and wraps it (wrap()), so values stay immutable and can be
 * shared.  OpenSSL only fails these calls on memory exhaustion, which
 * GLib treats as fatal anyway (g_error()); the exceptions callers can
 * provoke (a zero modulus, a negative difference, a bad hex string, a
 * failing random generator) are reported through GError.
 */

#include "config.h"
#include "lrg-big-num.h"

#include <string.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

struct _LrgBigNum
{
    gatomicrefcount  ref_count;
    BIGNUM          *bn;
};

G_DEFINE_QUARK (lrg-big-num-error-quark, lrg_big_num_error)

G_DEFINE_ENUM_TYPE (LrgBigNumError, lrg_big_num_error,
    G_DEFINE_ENUM_VALUE (LRG_BIG_NUM_ERROR_PARSE, "parse"),
    G_DEFINE_ENUM_VALUE (LRG_BIG_NUM_ERROR_NEGATIVE, "negative"),
    G_DEFINE_ENUM_VALUE (LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO, "division-by-zero"),
    G_DEFINE_ENUM_VALUE (LRG_BIG_NUM_ERROR_RANDOM, "random"),
    G_DEFINE_ENUM_VALUE (LRG_BIG_NUM_ERROR_FAILED, "failed"))

G_DEFINE_BOXED_TYPE (LrgBigNum, lrg_big_num, lrg_big_num_ref, lrg_big_num_unref)

/*
 * oom:
 * OpenSSL returns NULL or 0 from allocation-only failures.  Like
 * g_malloc(), treat running out of memory as fatal.
 */
static void
oom (const gchar *what) G_GNUC_NORETURN;

static void
oom (const gchar *what)
{
    g_error ("LrgBigNum: OpenSSL %s failed (out of memory)", what);
}

/*
 * wrap:
 * Takes ownership of @bn and returns it as a new LrgBigNum.
 */
static LrgBigNum *
wrap (BIGNUM *bn)
{
    LrgBigNum *self;

    if (bn == NULL)
        oom ("BN_new");
    self = g_new0 (LrgBigNum, 1);
    g_atomic_ref_count_init (&self->ref_count);
    self->bn = bn;
    return self;
}

/*
 * new_ctx:
 * A scratch context for multiplication, division and exponentiation.
 * Temporaries in a context are wiped when it is freed.
 */
static BN_CTX *
new_ctx (void)
{
    BN_CTX *ctx;

    ctx = BN_CTX_new ();
    if (ctx == NULL)
        oom ("BN_CTX_new");
    return ctx;
}

/*
 * check_modulus:
 * TRUE when @m can be used as a modulus; otherwise sets
 * DIVISION_BY_ZERO.
 */
static gboolean
check_modulus (LrgBigNum  *m,
               GError    **error)
{
    if (BN_is_zero (m->bn))
    {
        g_set_error_literal (error, LRG_BIG_NUM_ERROR,
                             LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO,
                             "Modulus is zero");
        return FALSE;
    }
    return TRUE;
}

LrgBigNum *
lrg_big_num_new_from_uint64 (guint64 value)
{
    BIGNUM *bn;
    guint8 be[8];
    guint i;

    /* BN_set_word() takes a BN_ULONG, which is 32 bits on some ABIs;
     * going through bytes works everywhere. */
    for (i = 0; i < 8; i++)
        be[i] = (guint8) (value >> (56 - 8 * i));
    bn = BN_bin2bn (be, sizeof be, NULL);
    return wrap (bn);
}

LrgBigNum *
lrg_big_num_new_from_data (const guint8 *data,
                           gsize         length,
                           LrgByteOrder  order)
{
    BIGNUM *bn;

    g_return_val_if_fail (data != NULL || length == 0, NULL);
    g_return_val_if_fail (length <= G_MAXINT, NULL);

    if (length == 0)
        bn = BN_new ();
    else if (order == LRG_BYTE_ORDER_LITTLE_ENDIAN)
        bn = BN_lebin2bn (data, (int) length, NULL);
    else
        bn = BN_bin2bn (data, (int) length, NULL);
    return wrap (bn);
}

LrgBigNum *
lrg_big_num_new_from_bytes (GBytes       *bytes,
                            LrgByteOrder  order)
{
    const guint8 *data;
    gsize length;

    g_return_val_if_fail (bytes != NULL, NULL);

    data = g_bytes_get_data (bytes, &length);
    return lrg_big_num_new_from_data (data, length, order);
}

LrgBigNum *
lrg_big_num_new_from_hex (const gchar  *hex,
                          GError      **error)
{
    const gchar *digits;
    BIGNUM *bn;
    gsize i;

    g_return_val_if_fail (hex != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Validate first: BN_hex2bn() accepts a sign and stops silently at
     * the first non-digit, both of which would hide bad input. */
    digits = hex;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits += 2;
    for (i = 0; digits[i] != '\0'; i++)
    {
        if (!g_ascii_isxdigit (digits[i]))
        {
            g_set_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE,
                         "Invalid hexadecimal digit '%c' at offset %" G_GSIZE_FORMAT,
                         digits[i], (gsize) (digits - hex) + i);
            return NULL;
        }
    }
    if (i == 0)
    {
        g_set_error_literal (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE,
                             "Hexadecimal number has no digits");
        return NULL;
    }
    if (i > (gsize) G_MAXINT / 4)
    {
        g_set_error_literal (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE,
                             "Hexadecimal number is too long");
        return NULL;
    }

    bn = NULL;
    if (BN_hex2bn (&bn, digits) == 0)
        oom ("BN_hex2bn");
    return wrap (bn);
}

LrgBigNum *
lrg_big_num_new_random (guint    n_bits,
                        GError **error)
{
    g_autofree guint8 *buf = NULL;
    BIGNUM *bn;
    gsize n_bytes;
    guint excess;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);
    g_return_val_if_fail (n_bits <= (guint) G_MAXINT, NULL);

    if (n_bits == 0)
        return wrap (BN_new ());

    /* Draw whole bytes, then clear the excess high bits of the most
     * significant byte so the value is below 2^n_bits. */
    n_bytes = (n_bits + 7) / 8;
    buf = g_malloc (n_bytes);
    if (RAND_bytes (buf, (int) n_bytes) != 1)
    {
        OPENSSL_cleanse (buf, n_bytes);
        g_set_error_literal (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_RANDOM,
                             "RAND_bytes() failed");
        return NULL;
    }
    excess = (guint) (n_bytes * 8 - n_bits);
    buf[0] &= (guint8) (0xFFu >> excess);

    bn = BN_bin2bn (buf, (int) n_bytes, NULL);
    OPENSSL_cleanse (buf, n_bytes);
    return wrap (bn);
}

LrgBigNum *
lrg_big_num_ref (LrgBigNum *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_ref_count_inc (&self->ref_count);
    return self;
}

void
lrg_big_num_unref (LrgBigNum *self)
{
    g_return_if_fail (self != NULL);

    if (!g_atomic_ref_count_dec (&self->ref_count))
        return;
    /* BN_clear_free() wipes the limbs before freeing them. */
    BN_clear_free (self->bn);
    g_free (self);
}

guint8 *
lrg_big_num_to_data (LrgBigNum    *self,
                     LrgByteOrder  order,
                     gsize         min_length,
                     gsize        *length)
{
    guint8 *buf;
    gsize n;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (length != NULL, NULL);
    g_return_val_if_fail (min_length <= G_MAXINT, NULL);

    /* MAX() with the natural length means the value is never cut. */
    n = MAX ((gsize) BN_num_bytes (self->bn), min_length);
    *length = n;
    buf = g_malloc0 (MAX (n, 1));
    if (n == 0)
        return buf;
    if (order == LRG_BYTE_ORDER_LITTLE_ENDIAN)
    {
        if (BN_bn2lebinpad (self->bn, buf, (int) n) < 0)
            oom ("BN_bn2lebinpad");
    }
    else
    {
        if (BN_bn2binpad (self->bn, buf, (int) n) < 0)
            oom ("BN_bn2binpad");
    }
    return buf;
}

GBytes *
lrg_big_num_to_bytes (LrgBigNum    *self,
                      LrgByteOrder  order,
                      gsize         min_length)
{
    guint8 *buf;
    gsize length;

    g_return_val_if_fail (self != NULL, NULL);

    buf = lrg_big_num_to_data (self, order, min_length, &length);
    return g_bytes_new_take (buf, length);
}

gchar *
lrg_big_num_to_hex (LrgBigNum *self)
{
    gchar *ossl;
    gchar *hex;
    const gchar *start;

    g_return_val_if_fail (self != NULL, NULL);

    ossl = BN_bn2hex (self->bn);
    if (ossl == NULL)
        oom ("BN_bn2hex");
    /* BN_bn2hex() writes whole bytes ("0A"); drop the leading zero
     * nibble so the output has no leading zeros, but keep "0". */
    start = ossl;
    while (start[0] == '0' && start[1] != '\0')
        start++;
    hex = g_strdup (start);
    OPENSSL_free (ossl);
    return hex;
}

gboolean
lrg_big_num_to_uint64 (LrgBigNum *self,
                       guint64   *value)
{
    guint8 be[8];
    guint64 v;
    guint i;

    g_return_val_if_fail (self != NULL, FALSE);

    if (BN_num_bytes (self->bn) > 8)
        return FALSE;
    if (BN_bn2binpad (self->bn, be, sizeof be) < 0)
        return FALSE;
    v = 0;
    for (i = 0; i < 8; i++)
        v = (v << 8) | be[i];
    if (value != NULL)
        *value = v;
    return TRUE;
}

gsize
lrg_big_num_get_num_bytes (LrgBigNum *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return (gsize) BN_num_bytes (self->bn);
}

guint
lrg_big_num_get_num_bits (LrgBigNum *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return (guint) BN_num_bits (self->bn);
}

gboolean
lrg_big_num_is_zero (LrgBigNum *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return BN_is_zero (self->bn) ? TRUE : FALSE;
}

gint
lrg_big_num_compare (LrgBigNum *a,
                     LrgBigNum *b)
{
    g_return_val_if_fail (a != NULL, 0);
    g_return_val_if_fail (b != NULL, 0);
    return BN_cmp (a->bn, b->bn);
}

gboolean
lrg_big_num_equal (LrgBigNum *a,
                   LrgBigNum *b)
{
    g_return_val_if_fail (a != NULL, FALSE);
    g_return_val_if_fail (b != NULL, FALSE);
    return BN_cmp (a->bn, b->bn) == 0;
}

LrgBigNum *
lrg_big_num_add (LrgBigNum *a,
                 LrgBigNum *b)
{
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    r = BN_new ();
    if (r == NULL || !BN_add (r, a->bn, b->bn))
        oom ("BN_add");
    return wrap (r);
}

LrgBigNum *
lrg_big_num_sub (LrgBigNum  *a,
                 LrgBigNum  *b,
                 GError    **error)
{
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (BN_cmp (a->bn, b->bn) < 0)
    {
        g_set_error_literal (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_NEGATIVE,
                             "Subtraction would give a negative number");
        return NULL;
    }
    r = BN_new ();
    if (r == NULL || !BN_sub (r, a->bn, b->bn))
        oom ("BN_sub");
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mul (LrgBigNum *a,
                 LrgBigNum *b)
{
    BN_CTX *ctx;
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    ctx = new_ctx ();
    r = BN_new ();
    if (r == NULL || !BN_mul (r, a->bn, b->bn, ctx))
        oom ("BN_mul");
    BN_CTX_free (ctx);
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mod (LrgBigNum  *a,
                 LrgBigNum  *m,
                 GError    **error)
{
    BN_CTX *ctx;
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (m != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!check_modulus (m, error))
        return NULL;
    ctx = new_ctx ();
    r = BN_new ();
    /* BN_nnmod() always yields the non-negative residue. */
    if (r == NULL || !BN_nnmod (r, a->bn, m->bn, ctx))
        oom ("BN_nnmod");
    BN_CTX_free (ctx);
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mod_add (LrgBigNum  *a,
                     LrgBigNum  *b,
                     LrgBigNum  *m,
                     GError    **error)
{
    BN_CTX *ctx;
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);
    g_return_val_if_fail (m != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!check_modulus (m, error))
        return NULL;
    ctx = new_ctx ();
    r = BN_new ();
    if (r == NULL || !BN_mod_add (r, a->bn, b->bn, m->bn, ctx))
        oom ("BN_mod_add");
    BN_CTX_free (ctx);
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mod_sub (LrgBigNum  *a,
                     LrgBigNum  *b,
                     LrgBigNum  *m,
                     GError    **error)
{
    BN_CTX *ctx;
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);
    g_return_val_if_fail (m != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!check_modulus (m, error))
        return NULL;
    ctx = new_ctx ();
    r = BN_new ();
    /* BN_mod_sub() returns the non-negative residue even when b > a. */
    if (r == NULL || !BN_mod_sub (r, a->bn, b->bn, m->bn, ctx))
        oom ("BN_mod_sub");
    BN_CTX_free (ctx);
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mod_mul (LrgBigNum  *a,
                     LrgBigNum  *b,
                     LrgBigNum  *m,
                     GError    **error)
{
    BN_CTX *ctx;
    BIGNUM *r;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);
    g_return_val_if_fail (m != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!check_modulus (m, error))
        return NULL;
    ctx = new_ctx ();
    r = BN_new ();
    if (r == NULL || !BN_mod_mul (r, a->bn, b->bn, m->bn, ctx))
        oom ("BN_mod_mul");
    BN_CTX_free (ctx);
    return wrap (r);
}

LrgBigNum *
lrg_big_num_mod_exp (LrgBigNum  *base,
                     LrgBigNum  *exponent,
                     LrgBigNum  *m,
                     GError    **error)
{
    BN_CTX *ctx;
    BIGNUM *exp;
    BIGNUM *r;
    int ok;

    g_return_val_if_fail (base != NULL, NULL);
    g_return_val_if_fail (exponent != NULL, NULL);
    g_return_val_if_fail (m != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!check_modulus (m, error))
        return NULL;

    /* The flag lives on the BIGNUM, and values are immutable, so flag a
     * private copy of the exponent.  OpenSSL only has a constant-time
     * path for odd moduli (Montgomery); with an even modulus the flag
     * would make BN_mod_exp() refuse, so leave it off there. */
    exp = BN_dup (exponent->bn);
    if (exp == NULL)
        oom ("BN_dup");
    if (BN_is_odd (m->bn))
        BN_set_flags (exp, BN_FLG_CONSTTIME);

    ctx = new_ctx ();
    r = BN_new ();
    if (r == NULL)
        oom ("BN_new");
    ok = BN_mod_exp (r, base->bn, exp, m->bn, ctx);
    BN_CTX_free (ctx);
    BN_clear_free (exp);
    if (!ok)
    {
        BN_clear_free (r);
        g_set_error_literal (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_FAILED,
                             "BN_mod_exp() failed");
        return NULL;
    }
    return wrap (r);
}
