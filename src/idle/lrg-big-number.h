/* lrg-big-number.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBigNumber - Arbitrary precision number for idle games.
 *
 * Big numbers are represented as mantissa * 10^exponent, allowing
 * representation of extremely large values like 1e100 or beyond.
 * Commonly used in idle/incremental games.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_BIG_NUMBER (lrg_big_number_get_type ())

/**
 * LrgBigNumber:
 * @mantissa: The mantissa value (1.0 <= m < 10.0)
 * @exponent: The power of 10
 * @is_zero: Whether the number is zero
 *
 * A boxed type representing a large number using mantissa * 10^exponent.
 * The mantissa is normalized to be in the range [1.0, 10.0).
 */
typedef struct _LrgBigNumber LrgBigNumber;

struct _LrgBigNumber
{
    gdouble mantissa;  /* 1.0 <= m < 10.0, or 0.0 if is_zero */
    gint64  exponent;  /* power of 10 */
    gboolean is_zero;  /* TRUE if number is exactly zero */
};

LRG_AVAILABLE_IN_ALL
GType lrg_big_number_get_type (void) G_GNUC_CONST;

/* Construction */

/**
 * lrg_big_number_new:
 * @value: Initial value
 *
 * Creates a new big number from a double value.
 *
 * Returns: (transfer full): A new #LrgBigNumber
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_new (gdouble value);

/**
 * lrg_big_number_new_from_parts:
 * @mantissa: Mantissa (will be normalized to [1.0, 10.0))
 * @exponent: Exponent (power of 10)
 *
 * Creates a new big number from mantissa and exponent.
 *
 * Returns: (transfer full): A new #LrgBigNumber
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_new_from_parts (gdouble mantissa,
                               gint64  exponent);

/**
 * lrg_big_number_new_zero:
 *
 * Creates a big number representing zero.
 *
 * Returns: (transfer full): A new #LrgBigNumber representing 0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_new_zero (void);

/**
 * lrg_big_number_copy:
 * @self: an #LrgBigNumber
 *
 * Creates a deep copy of a big number.
 *
 * Returns: (transfer full): A copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_copy (const LrgBigNumber *self);

/**
 * lrg_big_number_free:
 * @self: an #LrgBigNumber
 *
 * Frees a big number.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_big_number_free (LrgBigNumber *self);

/* Accessors */

/**
 * lrg_big_number_get_mantissa:
 * @self: an #LrgBigNumber
 *
 * Gets the mantissa (normalized to [1.0, 10.0)).
 *
 * Returns: The mantissa
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_big_number_get_mantissa (const LrgBigNumber *self);

/**
 * lrg_big_number_get_exponent:
 * @self: an #LrgBigNumber
 *
 * Gets the exponent (power of 10).
 *
 * Returns: The exponent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_big_number_get_exponent (const LrgBigNumber *self);

/**
 * lrg_big_number_is_zero:
 * @self: an #LrgBigNumber
 *
 * Checks if the number is zero.
 *
 * Returns: %TRUE if zero
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_big_number_is_zero (const LrgBigNumber *self);

/**
 * lrg_big_number_is_negative:
 * @self: an #LrgBigNumber
 *
 * Checks if the number is negative.
 *
 * Returns: %TRUE if negative
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_big_number_is_negative (const LrgBigNumber *self);

/**
 * lrg_big_number_to_double:
 * @self: an #LrgBigNumber
 *
 * Converts to double if possible. Returns infinity if too large.
 *
 * Returns: Double representation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_big_number_to_double (const LrgBigNumber *self);

/* Arithmetic */

/**
 * lrg_big_number_add:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Adds two big numbers.
 *
 * Returns: (transfer full): Result of a + b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_add (const LrgBigNumber *a,
                    const LrgBigNumber *b);

/**
 * lrg_big_number_subtract:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Subtracts two big numbers.
 *
 * Returns: (transfer full): Result of a - b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_subtract (const LrgBigNumber *a,
                         const LrgBigNumber *b);

/**
 * lrg_big_number_multiply:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Multiplies two big numbers.
 *
 * Returns: (transfer full): Result of a * b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_multiply (const LrgBigNumber *a,
                         const LrgBigNumber *b);

/**
 * lrg_big_number_divide:
 * @a: numerator #LrgBigNumber
 * @b: denominator #LrgBigNumber
 *
 * Divides two big numbers. Returns zero if b is zero.
 *
 * Returns: (transfer full): Result of a / b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_divide (const LrgBigNumber *a,
                       const LrgBigNumber *b);

/**
 * lrg_big_number_multiply_scalar:
 * @self: an #LrgBigNumber
 * @scalar: Scalar multiplier
 *
 * Multiplies by a scalar.
 *
 * Returns: (transfer full): Result of self * scalar
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_multiply_scalar (const LrgBigNumber *self,
                                gdouble             scalar);

/**
 * lrg_big_number_pow:
 * @self: an #LrgBigNumber
 * @exponent: Power to raise to
 *
 * Raises to a power.
 *
 * Returns: (transfer full): Result of self^exponent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_big_number_pow (const LrgBigNumber *self,
                    gdouble             exponent);

/* Comparison */

/**
 * lrg_big_number_compare:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Compares two big numbers.
 *
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_big_number_compare (const LrgBigNumber *a,
                        const LrgBigNumber *b);

/**
 * lrg_big_number_equals:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Checks if two big numbers are equal.
 *
 * Returns: %TRUE if equal
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_big_number_equals (const LrgBigNumber *a,
                       const LrgBigNumber *b);

/**
 * lrg_big_number_less_than:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Checks if a < b.
 *
 * Returns: %TRUE if a < b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_big_number_less_than (const LrgBigNumber *a,
                          const LrgBigNumber *b);

/**
 * lrg_big_number_greater_than:
 * @a: first #LrgBigNumber
 * @b: second #LrgBigNumber
 *
 * Checks if a > b.
 *
 * Returns: %TRUE if a > b
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_big_number_greater_than (const LrgBigNumber *a,
                             const LrgBigNumber *b);

/* Formatting */

/**
 * lrg_big_number_format_short:
 * @self: an #LrgBigNumber
 *
 * Formats with short suffix (K, M, B, T, Qa, Qi, Sx, Sp, Oc, No, Dc...).
 * Example: 1.5M, 2.3B, 4.7T
 *
 * Returns: (transfer full): Formatted string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_big_number_format_short (const LrgBigNumber *self);

/**
 * lrg_big_number_format_scientific:
 * @self: an #LrgBigNumber
 *
 * Formats in scientific notation.
 * Example: 1.50e6, 2.30e9
 *
 * Returns: (transfer full): Formatted string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_big_number_format_scientific (const LrgBigNumber *self);

/**
 * lrg_big_number_format_engineering:
 * @self: an #LrgBigNumber
 *
 * Formats in engineering notation (exponent multiple of 3).
 * Example: 1.50e6, 2.30e9
 *
 * Returns: (transfer full): Formatted string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_big_number_format_engineering (const LrgBigNumber *self);

/* In-place operations (modifies first argument) */

/**
 * lrg_big_number_add_in_place:
 * @self: an #LrgBigNumber to modify
 * @other: #LrgBigNumber to add
 *
 * Adds in place: self += other
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_big_number_add_in_place (LrgBigNumber       *self,
                             const LrgBigNumber *other);

/**
 * lrg_big_number_multiply_in_place:
 * @self: an #LrgBigNumber to modify
 * @scalar: Scalar multiplier
 *
 * Multiplies in place: self *= scalar
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_big_number_multiply_in_place (LrgBigNumber *self,
                                  gdouble       scalar);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBigNumber, lrg_big_number_free)

G_END_DECLS
