/* lrg-big-num.h - Immutable arbitrary-precision non-negative integers
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Key exchanges (SRP-6, Diffie-Hellman) and signature checks work on
 * integers hundreds or thousands of bits wide.  An #LrgBigNum is such
 * an integer: immutable, reference counted and backed by OpenSSL's
 * BIGNUM.  Every arithmetic function returns a new value, so a number
 * can be shared freely between objects and threads.  Only non-negative
 * values exist; lrg_big_num_sub() fails rather than go below zero.
 */

#ifndef LRG_BIG_NUM_H
#define LRG_BIG_NUM_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../io/lrg-byte-reader.h"

G_BEGIN_DECLS

/**
 * LRG_BIG_NUM_ERROR:
 *
 * Error domain for #LrgBigNum.  Errors carry an #LrgBigNumError code.
 */
#define LRG_BIG_NUM_ERROR (lrg_big_num_error_quark ())

/**
 * LrgBigNumError:
 * @LRG_BIG_NUM_ERROR_PARSE: a string is not a valid hexadecimal number
 * @LRG_BIG_NUM_ERROR_NEGATIVE: the result would be negative
 * @LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO: a modulus is zero
 * @LRG_BIG_NUM_ERROR_RANDOM: the random number generator failed
 * @LRG_BIG_NUM_ERROR_FAILED: OpenSSL reported another failure (usually
 *   memory exhaustion)
 *
 * Error codes for the %LRG_BIG_NUM_ERROR domain.
 */
typedef enum
{
    LRG_BIG_NUM_ERROR_PARSE,
    LRG_BIG_NUM_ERROR_NEGATIVE,
    LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO,
    LRG_BIG_NUM_ERROR_RANDOM,
    LRG_BIG_NUM_ERROR_FAILED
} LrgBigNumError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_big_num_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_big_num_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BIG_NUM_ERROR (lrg_big_num_error_get_type ())

#define LRG_TYPE_BIG_NUM (lrg_big_num_get_type ())

/**
 * LrgBigNum:
 *
 * An immutable, reference-counted, non-negative integer of any size.
 * The memory holding the value is wiped when the last reference is
 * dropped, so secrets (private keys, shared secrets) do not linger.
 */
typedef struct _LrgBigNum LrgBigNum;

LRG_AVAILABLE_IN_ALL
GType lrg_big_num_get_type (void) G_GNUC_CONST;

/**
 * lrg_big_num_new_from_uint64:
 * @value: the value
 *
 * Returns: (transfer full): a number equal to @value
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_new_from_uint64 (guint64 value);

/**
 * lrg_big_num_new_from_data:
 * @data: (array length=length) (element-type guint8) (nullable): the
 *   magnitude, unsigned
 * @length: the length of @data
 * @order: whether @data starts with the least or the most significant
 *   byte
 *
 * Reads an unsigned integer of @length bytes.  Leading zero bytes are
 * allowed and do not change the value; @length 0 reads zero.
 *
 * Returns: (transfer full): the number
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_new_from_data (const guint8 *data,
                                      gsize         length,
                                      LrgByteOrder  order);

/**
 * lrg_big_num_new_from_bytes:
 * @bytes: the magnitude, unsigned
 * @order: whether @bytes starts with the least or the most significant
 *   byte
 *
 * lrg_big_num_new_from_data() on the contents of @bytes.
 *
 * Returns: (transfer full): the number
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_new_from_bytes (GBytes       *bytes,
                                       LrgByteOrder  order);

/**
 * lrg_big_num_new_from_hex:
 * @hex: hexadecimal digits, most significant first, optionally prefixed
 *   with "0x" or "0X"; upper and lower case are both accepted
 * @error: return location for a #GError
 *
 * Parses a hexadecimal number.  Signs, whitespace and an empty digit
 * string are rejected with %LRG_BIG_NUM_ERROR_PARSE.
 *
 * Returns: (transfer full) (nullable): the number, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_new_from_hex (const gchar  *hex,
                                     GError      **error);

/**
 * lrg_big_num_new_random:
 * @n_bits: the number of random bits
 * @error: return location for a #GError
 *
 * Draws a uniformly random number in [0, 2^@n_bits) from OpenSSL's
 * cryptographically secure generator (RAND_bytes()).  The top bit is
 * not forced, so the result may have fewer than @n_bits significant
 * bits.  @n_bits 0 returns zero.
 *
 * Returns: (transfer full) (nullable): the number, or %NULL if the
 *   generator failed (%LRG_BIG_NUM_ERROR_RANDOM)
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_new_random (guint    n_bits,
                                   GError **error);

/**
 * lrg_big_num_ref:
 * @self: an #LrgBigNum
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_ref (LrgBigNum *self);

/**
 * lrg_big_num_unref:
 * @self: (transfer full): an #LrgBigNum
 *
 * Drops a reference; the last one wipes and frees the value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_big_num_unref (LrgBigNum *self);

/**
 * lrg_big_num_to_data:
 * @self: an #LrgBigNum
 * @order: the byte order to write
 * @min_length: the minimum length of the result
 * @length: (out): the length of the result
 *
 * Writes the magnitude, zero-padded on the most significant side to at
 * least @min_length bytes.  A number that needs more than @min_length
 * bytes is never truncated: the result is then lrg_big_num_get_num_bytes()
 * long.  Zero with @min_length 0 gives an empty result.
 *
 * Returns: (transfer full) (array length=length) (element-type guint8):
 *   the bytes; free with g_free()
 */
LRG_AVAILABLE_IN_ALL
guint8 *lrg_big_num_to_data (LrgBigNum    *self,
                             LrgByteOrder  order,
                             gsize         min_length,
                             gsize        *length);

/**
 * lrg_big_num_to_bytes:
 * @self: an #LrgBigNum
 * @order: the byte order to write
 * @min_length: the minimum length of the result
 *
 * lrg_big_num_to_data() as a #GBytes.
 *
 * Returns: (transfer full): the bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_big_num_to_bytes (LrgBigNum    *self,
                              LrgByteOrder  order,
                              gsize         min_length);

/**
 * lrg_big_num_to_hex:
 * @self: an #LrgBigNum
 *
 * Formats the number as upper-case hexadecimal, most significant digit
 * first, without a prefix or leading zeros.  Zero is "0".
 *
 * Returns: (transfer full): the digits
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_big_num_to_hex (LrgBigNum *self);

/**
 * lrg_big_num_to_uint64:
 * @self: an #LrgBigNum
 * @value: (out) (optional): return location for the value
 *
 * Returns: %TRUE if the number fits in 64 bits (and @value is set)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_big_num_to_uint64 (LrgBigNum *self,
                                guint64   *value);

/**
 * lrg_big_num_get_num_bytes:
 * @self: an #LrgBigNum
 *
 * Returns: the number of bytes the magnitude needs (0 for zero)
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_big_num_get_num_bytes (LrgBigNum *self);

/**
 * lrg_big_num_get_num_bits:
 * @self: an #LrgBigNum
 *
 * Returns: the position of the highest set bit plus one (0 for zero)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_big_num_get_num_bits (LrgBigNum *self);

/**
 * lrg_big_num_is_zero:
 * @self: an #LrgBigNum
 *
 * Returns: %TRUE if @self is zero
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_big_num_is_zero (LrgBigNum *self);

/**
 * lrg_big_num_compare:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 *
 * Returns: a negative value if @a < @b, 0 if equal, positive if @a > @b
 */
LRG_AVAILABLE_IN_ALL
gint lrg_big_num_compare (LrgBigNum *a,
                          LrgBigNum *b);

/**
 * lrg_big_num_equal:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 *
 * Returns: %TRUE if @a and @b have the same value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_big_num_equal (LrgBigNum *a,
                            LrgBigNum *b);

/**
 * lrg_big_num_add:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 *
 * Returns: (transfer full): @a + @b
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_add (LrgBigNum *a,
                            LrgBigNum *b);

/**
 * lrg_big_num_sub:
 * @a: the minuend
 * @b: the subtrahend
 * @error: return location for a #GError
 *
 * Numbers are never negative: when @b > @a this fails with
 * %LRG_BIG_NUM_ERROR_NEGATIVE.  Use lrg_big_num_mod_sub() for modular
 * subtraction.
 *
 * Returns: (transfer full) (nullable): @a - @b, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_sub (LrgBigNum  *a,
                            LrgBigNum  *b,
                            GError    **error);

/**
 * lrg_big_num_mul:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 *
 * Returns: (transfer full): @a * @b
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mul (LrgBigNum *a,
                            LrgBigNum *b);

/**
 * lrg_big_num_mod:
 * @a: an #LrgBigNum
 * @m: the modulus
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): @a mod @m, or %NULL if @m is zero
 *   (%LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO)
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mod (LrgBigNum  *a,
                            LrgBigNum  *m,
                            GError    **error);

/**
 * lrg_big_num_mod_add:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 * @m: the modulus
 * @error: return location for a #GError
 *
 * @a and @b need not be reduced.
 *
 * Returns: (transfer full) (nullable): (@a + @b) mod @m, or %NULL if @m
 *   is zero
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mod_add (LrgBigNum  *a,
                                LrgBigNum  *b,
                                LrgBigNum  *m,
                                GError    **error);

/**
 * lrg_big_num_mod_sub:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 * @m: the modulus
 * @error: return location for a #GError
 *
 * The result is the non-negative residue in [0, @m), whichever of @a
 * and @b is larger.  @a and @b need not be reduced.
 *
 * Returns: (transfer full) (nullable): (@a - @b) mod @m, or %NULL if @m
 *   is zero
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mod_sub (LrgBigNum  *a,
                                LrgBigNum  *b,
                                LrgBigNum  *m,
                                GError    **error);

/**
 * lrg_big_num_mod_mul:
 * @a: an #LrgBigNum
 * @b: an #LrgBigNum
 * @m: the modulus
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): (@a * @b) mod @m, or %NULL if @m
 *   is zero
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mod_mul (LrgBigNum  *a,
                                LrgBigNum  *b,
                                LrgBigNum  *m,
                                GError    **error);

/**
 * lrg_big_num_mod_exp:
 * @base: the base
 * @exponent: the exponent
 * @m: the modulus
 * @error: return location for a #GError
 *
 * Modular exponentiation.  The exponent is treated as a secret: when
 * @m is odd (every prime-group modulus is), OpenSSL's constant-time
 * Montgomery ladder is used, so the running time does not depend on
 * the exponent's bits.  An even modulus falls back to the ordinary
 * algorithm.
 *
 * Returns: (transfer full) (nullable): @base ^ @exponent mod @m, or
 *   %NULL if @m is zero
 */
LRG_AVAILABLE_IN_ALL
LrgBigNum *lrg_big_num_mod_exp (LrgBigNum  *base,
                                LrgBigNum  *exponent,
                                LrgBigNum  *m,
                                GError    **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBigNum, lrg_big_num_unref)

G_END_DECLS

#endif /* LRG_BIG_NUM_H */
