/* lrg-big-number.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-big-number.h"

#include <math.h>
#include <string.h>

/*
 * Short suffixes for number formatting.
 * Standard idle game notation up to 10^303.
 */
static const gchar *suffixes[] = {
    "",     /* 10^0 */
    "K",    /* 10^3  - Thousand */
    "M",    /* 10^6  - Million */
    "B",    /* 10^9  - Billion */
    "T",    /* 10^12 - Trillion */
    "Qa",   /* 10^15 - Quadrillion */
    "Qi",   /* 10^18 - Quintillion */
    "Sx",   /* 10^21 - Sextillion */
    "Sp",   /* 10^24 - Septillion */
    "Oc",   /* 10^27 - Octillion */
    "No",   /* 10^30 - Nonillion */
    "Dc",   /* 10^33 - Decillion */
    "UDc",  /* 10^36 - Undecillion */
    "DDc",  /* 10^39 - Duodecillion */
    "TDc",  /* 10^42 - Tredecillion */
    "QaDc", /* 10^45 - Quattuordecillion */
    "QiDc", /* 10^48 - Quindecillion */
    "SxDc", /* 10^51 - Sexdecillion */
    "SpDc", /* 10^54 - Septendecillion */
    "OcDc", /* 10^57 - Octodecillion */
    "NoDc", /* 10^60 - Novemdecillion */
    "Vg",   /* 10^63 - Vigintillion */
    NULL
};

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgBigNumber *lrg_big_number_copy (const LrgBigNumber *src);
void lrg_big_number_free (LrgBigNumber *self);

G_DEFINE_BOXED_TYPE (LrgBigNumber, lrg_big_number,
                     lrg_big_number_copy,
                     lrg_big_number_free)

/*
 * Normalizes mantissa to be in [1.0, 10.0) and adjusts exponent.
 */
static void
normalize (LrgBigNumber *self)
{
    gboolean negative;

    if (self->mantissa == 0.0)
    {
        self->is_zero = TRUE;
        self->exponent = 0;
        return;
    }

    self->is_zero = FALSE;

    /* Handle negative mantissa */
    negative = self->mantissa < 0.0;
    if (negative)
        self->mantissa = -self->mantissa;

    /* Normalize to [1.0, 10.0) */
    while (self->mantissa >= 10.0)
    {
        self->mantissa /= 10.0;
        self->exponent++;
    }

    while (self->mantissa < 1.0 && self->mantissa > 0.0)
    {
        self->mantissa *= 10.0;
        self->exponent--;
    }

    if (negative)
        self->mantissa = -self->mantissa;
}

LrgBigNumber *
lrg_big_number_new (gdouble value)
{
    LrgBigNumber *self;

    self = g_slice_new0 (LrgBigNumber);

    if (value == 0.0)
    {
        self->mantissa = 0.0;
        self->exponent = 0;
        self->is_zero = TRUE;
    }
    else
    {
        self->mantissa = value;
        self->exponent = 0;
        self->is_zero = FALSE;
        normalize (self);
    }

    return self;
}

LrgBigNumber *
lrg_big_number_new_from_parts (gdouble mantissa,
                               gint64  exponent)
{
    LrgBigNumber *self;

    self = g_slice_new0 (LrgBigNumber);
    self->mantissa = mantissa;
    self->exponent = exponent;
    self->is_zero = FALSE;

    normalize (self);

    return self;
}

LrgBigNumber *
lrg_big_number_new_zero (void)
{
    LrgBigNumber *self;

    self = g_slice_new0 (LrgBigNumber);
    self->mantissa = 0.0;
    self->exponent = 0;
    self->is_zero = TRUE;

    return self;
}

LrgBigNumber *
lrg_big_number_copy (const LrgBigNumber *self)
{
    LrgBigNumber *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgBigNumber);
    copy->mantissa = self->mantissa;
    copy->exponent = self->exponent;
    copy->is_zero = self->is_zero;

    return copy;
}

void
lrg_big_number_free (LrgBigNumber *self)
{
    if (self == NULL)
        return;

    g_slice_free (LrgBigNumber, self);
}

gdouble
lrg_big_number_get_mantissa (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->mantissa;
}

gint64
lrg_big_number_get_exponent (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->exponent;
}

gboolean
lrg_big_number_is_zero (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return self->is_zero;
}

gboolean
lrg_big_number_is_negative (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return !self->is_zero && self->mantissa < 0.0;
}

gdouble
lrg_big_number_to_double (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, 0.0);

    if (self->is_zero)
        return 0.0;

    /* Check for overflow */
    if (self->exponent > 308)
        return self->mantissa > 0 ? G_MAXDOUBLE : -G_MAXDOUBLE;

    if (self->exponent < -308)
        return 0.0;

    return self->mantissa * pow (10.0, (gdouble)self->exponent);
}

LrgBigNumber *
lrg_big_number_add (const LrgBigNumber *a,
                    const LrgBigNumber *b)
{
    LrgBigNumber *result;
    gdouble a_val, b_val;
    gint64 max_exp;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    if (a->is_zero)
        return lrg_big_number_copy (b);
    if (b->is_zero)
        return lrg_big_number_copy (a);

    /*
     * Align exponents to the larger one.
     * Scale the smaller number down.
     */
    max_exp = MAX (a->exponent, b->exponent);

    /* Scale mantissas to common exponent */
    a_val = a->mantissa * pow (10.0, (gdouble)(a->exponent - max_exp));
    b_val = b->mantissa * pow (10.0, (gdouble)(b->exponent - max_exp));

    result = g_slice_new0 (LrgBigNumber);
    result->mantissa = a_val + b_val;
    result->exponent = max_exp;
    result->is_zero = FALSE;

    normalize (result);

    return result;
}

LrgBigNumber *
lrg_big_number_subtract (const LrgBigNumber *a,
                         const LrgBigNumber *b)
{
    LrgBigNumber *result;
    gdouble a_val, b_val;
    gint64 max_exp;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    if (b->is_zero)
        return lrg_big_number_copy (a);
    if (a->is_zero)
    {
        result = lrg_big_number_copy (b);
        result->mantissa = -result->mantissa;
        return result;
    }

    max_exp = MAX (a->exponent, b->exponent);

    a_val = a->mantissa * pow (10.0, (gdouble)(a->exponent - max_exp));
    b_val = b->mantissa * pow (10.0, (gdouble)(b->exponent - max_exp));

    result = g_slice_new0 (LrgBigNumber);
    result->mantissa = a_val - b_val;
    result->exponent = max_exp;
    result->is_zero = FALSE;

    normalize (result);

    return result;
}

LrgBigNumber *
lrg_big_number_multiply (const LrgBigNumber *a,
                         const LrgBigNumber *b)
{
    LrgBigNumber *result;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    if (a->is_zero || b->is_zero)
        return lrg_big_number_new_zero ();

    result = g_slice_new0 (LrgBigNumber);
    result->mantissa = a->mantissa * b->mantissa;
    result->exponent = a->exponent + b->exponent;
    result->is_zero = FALSE;

    normalize (result);

    return result;
}

LrgBigNumber *
lrg_big_number_divide (const LrgBigNumber *a,
                       const LrgBigNumber *b)
{
    LrgBigNumber *result;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    if (b->is_zero)
        return lrg_big_number_new_zero ();

    if (a->is_zero)
        return lrg_big_number_new_zero ();

    result = g_slice_new0 (LrgBigNumber);
    result->mantissa = a->mantissa / b->mantissa;
    result->exponent = a->exponent - b->exponent;
    result->is_zero = FALSE;

    normalize (result);

    return result;
}

LrgBigNumber *
lrg_big_number_multiply_scalar (const LrgBigNumber *self,
                                gdouble             scalar)
{
    LrgBigNumber *result;

    g_return_val_if_fail (self != NULL, NULL);

    if (self->is_zero || scalar == 0.0)
        return lrg_big_number_new_zero ();

    result = g_slice_new0 (LrgBigNumber);
    result->mantissa = self->mantissa * scalar;
    result->exponent = self->exponent;
    result->is_zero = FALSE;

    normalize (result);

    return result;
}

LrgBigNumber *
lrg_big_number_pow (const LrgBigNumber *self,
                    gdouble             exponent)
{
    LrgBigNumber *result;
    gdouble new_exp;
    gdouble frac_exp;

    g_return_val_if_fail (self != NULL, NULL);

    if (self->is_zero)
        return lrg_big_number_new_zero ();

    if (exponent == 0.0)
        return lrg_big_number_new (1.0);

    /*
     * For (m * 10^e)^p = m^p * 10^(e*p)
     * But m^p may need renormalization.
     */
    result = g_slice_new0 (LrgBigNumber);

    /* Calculate new exponent */
    new_exp = (gdouble)self->exponent * exponent;

    /* Handle the mantissa power */
    result->mantissa = pow (self->mantissa, exponent);

    /* The fractional part of the exponent affects the mantissa */
    result->exponent = (gint64)floor (new_exp);
    frac_exp = new_exp - (gdouble)result->exponent;
    result->mantissa *= pow (10.0, frac_exp);

    result->is_zero = FALSE;

    normalize (result);

    return result;
}

gint
lrg_big_number_compare (const LrgBigNumber *a,
                        const LrgBigNumber *b)
{
    gboolean a_neg;
    gboolean b_neg;

    g_return_val_if_fail (a != NULL, 0);
    g_return_val_if_fail (b != NULL, 0);

    /* Handle zeros */
    if (a->is_zero && b->is_zero)
        return 0;
    if (a->is_zero)
        return b->mantissa > 0 ? -1 : 1;
    if (b->is_zero)
        return a->mantissa > 0 ? 1 : -1;

    /* Handle different signs */
    a_neg = a->mantissa < 0;
    b_neg = b->mantissa < 0;

    if (a_neg && !b_neg)
        return -1;
    if (!a_neg && b_neg)
        return 1;

    /* Same sign - compare exponents first */
    if (a->exponent > b->exponent)
        return a_neg ? -1 : 1;
    if (a->exponent < b->exponent)
        return a_neg ? 1 : -1;

    /* Same exponent - compare mantissas */
    if (a->mantissa > b->mantissa)
        return 1;
    if (a->mantissa < b->mantissa)
        return -1;

    return 0;
}

gboolean
lrg_big_number_equals (const LrgBigNumber *a,
                       const LrgBigNumber *b)
{
    return lrg_big_number_compare (a, b) == 0;
}

gboolean
lrg_big_number_less_than (const LrgBigNumber *a,
                          const LrgBigNumber *b)
{
    return lrg_big_number_compare (a, b) < 0;
}

gboolean
lrg_big_number_greater_than (const LrgBigNumber *a,
                             const LrgBigNumber *b)
{
    return lrg_big_number_compare (a, b) > 0;
}

gchar *
lrg_big_number_format_short (const LrgBigNumber *self)
{
    gint suffix_index;
    gdouble display_value;
    gint64 display_exp;
    gint max_suffix;

    g_return_val_if_fail (self != NULL, NULL);

    if (self->is_zero)
        return g_strdup ("0");

    /*
     * Find appropriate suffix.
     * Each suffix represents 3 orders of magnitude.
     */
    suffix_index = (gint)(self->exponent / 3);

    if (suffix_index < 0)
    {
        /* Very small number - just show it */
        return g_strdup_printf ("%.2e", lrg_big_number_to_double (self));
    }

    /* Calculate how many suffixes we have */
    max_suffix = 0;
    while (suffixes[max_suffix + 1] != NULL)
        max_suffix++;

    if (suffix_index > max_suffix)
    {
        /* Too large for suffixes - use scientific notation */
        return lrg_big_number_format_scientific (self);
    }

    /* Calculate display value */
    display_exp = self->exponent - (suffix_index * 3);
    display_value = self->mantissa * pow (10.0, (gdouble)display_exp);

    /* Format with suffix */
    if (suffix_index == 0)
    {
        /* No suffix needed for small numbers */
        if (display_exp >= 0)
            return g_strdup_printf ("%.0f", display_value);
        else
            return g_strdup_printf ("%.2f", display_value);
    }

    return g_strdup_printf ("%.2f%s", display_value, suffixes[suffix_index]);
}

gchar *
lrg_big_number_format_scientific (const LrgBigNumber *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    if (self->is_zero)
        return g_strdup ("0");

    return g_strdup_printf ("%.2fe%" G_GINT64_FORMAT,
                            self->mantissa, self->exponent);
}

gchar *
lrg_big_number_format_engineering (const LrgBigNumber *self)
{
    gint64 eng_exp;
    gdouble display_value;
    gint64 remainder;

    g_return_val_if_fail (self != NULL, NULL);

    if (self->is_zero)
        return g_strdup ("0");

    /* Round exponent down to multiple of 3 */
    eng_exp = (self->exponent / 3) * 3;
    if (self->exponent < 0 && self->exponent % 3 != 0)
        eng_exp -= 3;

    remainder = self->exponent - eng_exp;
    display_value = self->mantissa * pow (10.0, (gdouble)remainder);

    return g_strdup_printf ("%.2fe%" G_GINT64_FORMAT, display_value, eng_exp);
}

void
lrg_big_number_add_in_place (LrgBigNumber       *self,
                             const LrgBigNumber *other)
{
    gdouble a_val, b_val;
    gint64 max_exp;

    g_return_if_fail (self != NULL);
    g_return_if_fail (other != NULL);

    if (other->is_zero)
        return;

    if (self->is_zero)
    {
        self->mantissa = other->mantissa;
        self->exponent = other->exponent;
        self->is_zero = other->is_zero;
        return;
    }

    max_exp = MAX (self->exponent, other->exponent);

    a_val = self->mantissa * pow (10.0, (gdouble)(self->exponent - max_exp));
    b_val = other->mantissa * pow (10.0, (gdouble)(other->exponent - max_exp));

    self->mantissa = a_val + b_val;
    self->exponent = max_exp;

    normalize (self);
}

void
lrg_big_number_multiply_in_place (LrgBigNumber *self,
                                  gdouble       scalar)
{
    g_return_if_fail (self != NULL);

    if (self->is_zero)
        return;

    if (scalar == 0.0)
    {
        self->mantissa = 0.0;
        self->exponent = 0;
        self->is_zero = TRUE;
        return;
    }

    self->mantissa *= scalar;
    normalize (self);
}
