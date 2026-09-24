/* test-experience-curve.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgExperienceCurve: formula and table curves, saturating
 * awards across many levels, rested accrual/consumption and validation.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>

#include "lrg-enums.h"
#include "progression/lrg-experience-curve.h"

/* Counts notify emissions for one property. */
static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    (void) object;
    (void) pspec;
    (*(guint *) user_data)++;
}

/* Expects exactly one g_return_if_fail critical from the library. */
static void
expect_critical (void)
{
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*failed*");
}

/* ========================================================================== */
/*                         Construction and properties                        */
/* ========================================================================== */

static void
test_defaults (void)
{
    g_autoptr(LrgExperienceCurve) curve = NULL;
    gdouble base;
    gdouble linear;
    gdouble quadratic;
    gdouble cubic;

    curve = lrg_experience_curve_new (60);
    g_assert_nonnull (curve);
    g_assert_cmpuint (lrg_experience_curve_get_level_cap (curve), ==, 60);
    g_assert_cmpfloat_with_epsilon (lrg_experience_curve_get_rested_rate (curve), 0.05 / 8.0, 1e-12);
    g_assert_cmpfloat (lrg_experience_curve_get_rested_cap (curve), ==, 1.5);
    g_assert_cmpfloat (lrg_experience_curve_get_rested_multiplier (curve), ==, 2.0);
    g_assert_false (lrg_experience_curve_has_table (curve));

    lrg_experience_curve_get_formula (curve, &base, &linear, &quadratic, &cubic);
    g_assert_cmpfloat (base, ==, 100.0);
    g_assert_cmpfloat (linear, ==, 100.0);
    g_assert_cmpfloat (quadratic, ==, 10.0);
    g_assert_cmpfloat (cubic, ==, 0.0);
    /* optional out parameters */
    lrg_experience_curve_get_formula (curve, NULL, NULL, NULL, NULL);

    /* g_object_new without arguments uses the property default */
    {
        g_autoptr(LrgExperienceCurve) plain = g_object_new (LRG_TYPE_EXPERIENCE_CURVE, NULL);

        g_assert_cmpuint (lrg_experience_curve_get_level_cap (plain), ==, 60);
    }
}

static void
test_properties (void)
{
    g_autoptr(LrgExperienceCurve) curve = NULL;
    guint cap;
    gdouble rate;
    gdouble rcap;
    gdouble mult;
    gdouble base;
    gdouble linear;
    gdouble quadratic;
    gdouble cubic;

    curve = lrg_experience_curve_new (10);
    g_object_set (curve,
                  "level-cap", 20,
                  "rested-rate", 0.5,
                  "rested-cap", 3.0,
                  "rested-multiplier", 1.5,
                  "formula-base", 7.0,
                  "formula-linear", 1.0,
                  "formula-quadratic", 2.0,
                  "formula-cubic", 0.5,
                  NULL);
    g_object_get (curve,
                  "level-cap", &cap,
                  "rested-rate", &rate,
                  "rested-cap", &rcap,
                  "rested-multiplier", &mult,
                  "formula-base", &base,
                  "formula-linear", &linear,
                  "formula-quadratic", &quadratic,
                  "formula-cubic", &cubic,
                  NULL);
    g_assert_cmpuint (cap, ==, 20);
    g_assert_cmpfloat (rate, ==, 0.5);
    g_assert_cmpfloat (rcap, ==, 3.0);
    g_assert_cmpfloat (mult, ==, 1.5);
    g_assert_cmpfloat (base, ==, 7.0);
    g_assert_cmpfloat (linear, ==, 1.0);
    g_assert_cmpfloat (quadratic, ==, 2.0);
    g_assert_cmpfloat (cubic, ==, 0.5);

    /* formula properties drive get_xp_for_level: 7 + 2 + 8 + 4 = 21 at L=2 */
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 2), ==, 21);
}

static void
test_notify_only_on_change (void)
{
    g_autoptr(LrgExperienceCurve) curve = NULL;
    guint notified = 0;

    curve = lrg_experience_curve_new (60);
    g_signal_connect (curve, "notify::rested-rate", G_CALLBACK (count_notify), &notified);

    lrg_experience_curve_set_rested_rate (curve, lrg_experience_curve_get_rested_rate (curve));
    g_assert_cmpuint (notified, ==, 0);
    lrg_experience_curve_set_rested_rate (curve, 0.25);
    g_assert_cmpuint (notified, ==, 1);
    lrg_experience_curve_set_rested_rate (curve, 0.25);
    g_assert_cmpuint (notified, ==, 1);
    g_object_set (curve, "rested-rate", 0.5, NULL);
    g_assert_cmpuint (notified, ==, 2);
}

static void
test_setter_rejections (void)
{
    g_autoptr(LrgExperienceCurve) curve = NULL;

    curve = lrg_experience_curve_new (60);

    expect_critical ();
    lrg_experience_curve_set_level_cap (curve, 0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_level_cap (curve, LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP + 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_experience_curve_get_level_cap (curve), ==, 60);

    expect_critical ();
    lrg_experience_curve_set_rested_rate (curve, NAN);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_rested_rate (curve, -0.1);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_rested_cap (curve, INFINITY);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_rested_multiplier (curve, 0.5);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_formula (curve, NAN, 0.0, 0.0, 0.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_experience_curve_set_formula (curve, 0.0, 0.0, 0.0, -INFINITY);
    g_test_assert_expected_messages ();

    g_assert_cmpfloat_with_epsilon (lrg_experience_curve_get_rested_rate (curve), 0.05 / 8.0, 1e-12);
    g_assert_cmpfloat (lrg_experience_curve_get_rested_cap (curve), ==, 1.5);
    g_assert_cmpfloat (lrg_experience_curve_get_rested_multiplier (curve), ==, 2.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 210);

    expect_critical ();
    g_assert_null (lrg_experience_curve_new (0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_experience_curve_new (1001));
    g_test_assert_expected_messages ();
}

/* ========================================================================== */
/*                               Formula curve                                */
/* ========================================================================== */

static void
test_formula_levels (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (60);

    /* 100 + 100L + 10L^2 */
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 0), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 210);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 2), ==, 340);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 59), ==, 100 + 5900 + 34810);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 60), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 61), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, G_MAXUINT), ==, 0);

    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 0), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 1), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 2), ==, 210);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 3), ==, 550);
    /* beyond the cap is treated as the cap */
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 500), ==,
                      lrg_experience_curve_get_total_xp_for_level (curve, 60));
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 60), ==,
                      lrg_experience_curve_get_total_xp_for_level (curve, 59) +
                      lrg_experience_curve_get_xp_for_level (curve, 59));
}

static void
test_formula_rounding_and_minimum (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (10);

    lrg_experience_curve_set_formula (curve, 0.0, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 1);

    lrg_experience_curve_set_formula (curve, -500.0, -1.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 5), ==, 1);

    lrg_experience_curve_set_formula (curve, 1.4, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 1);
    lrg_experience_curve_set_formula (curve, 2.5, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 3);
    lrg_experience_curve_set_formula (curve, 2.49, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 2);

    /* cubic term: 1 + L^3 at L = 3 is 28 */
    lrg_experience_curve_set_formula (curve, 1.0, 0.0, 0.0, 1.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 3), ==, 28);
}

static void
test_formula_saturates (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (1000);

    lrg_experience_curve_set_formula (curve, 0.0, 0.0, 0.0, 1e300);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 999), ==, G_MAXUINT64);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 1000), ==, G_MAXUINT64);

    /* exactly 2^64 rounds to the saturated maximum */
    lrg_experience_curve_set_formula (curve, 18446744073709551616.0, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, G_MAXUINT64);
}

/* ========================================================================== */
/*                                Table curve                                 */
/* ========================================================================== */

static void
test_table_overrides_formula (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (4);
    g_autoptr(GError) error = NULL;
    const guint64 table[] = { 100, 200, 300 };

    g_assert_true (lrg_experience_curve_set_table (curve, table, G_N_ELEMENTS (table), &error));
    g_assert_no_error (error);
    g_assert_true (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 100);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 2), ==, 200);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 3), ==, 300);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 4), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 4), ==, 600);

    /* the curve copied the table */
    {
        guint64 mutable_table[] = { 5, 6, 7 };

        g_assert_true (lrg_experience_curve_set_table (curve, mutable_table, 3, NULL));
        mutable_table[0] = 999;
        g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 5);
    }

    /* clearing returns to the formula */
    lrg_experience_curve_clear_table (curve);
    g_assert_false (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 210);

    /* a new formula also drops a table */
    g_assert_true (lrg_experience_curve_set_table (curve, table, 3, NULL));
    lrg_experience_curve_set_formula (curve, 50.0, 0.0, 0.0, 0.0);
    g_assert_false (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 50);

    /* as does changing the level cap */
    g_assert_true (lrg_experience_curve_set_table (curve, table, 3, NULL));
    lrg_experience_curve_set_level_cap (curve, 5);
    g_assert_false (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 4), ==, 50);
}

static void
test_table_rejections (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (4);
    g_autoptr(GError) error = NULL;
    const guint64 good[] = { 10, 20, 30 };
    const guint64 short_table[] = { 10, 20 };
    const guint64 long_table[] = { 10, 20, 30, 40 };
    const guint64 zero_entry[] = { 10, 0, 30 };

    g_assert_true (lrg_experience_curve_set_table (curve, good, 3, NULL));

    g_assert_false (lrg_experience_curve_set_table (curve, short_table, 2, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_set_table (curve, long_table, 4, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_set_table (curve, zero_entry, 3, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_set_table (curve, NULL, 3, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* NULL error pointer is allowed on failure */
    g_assert_false (lrg_experience_curve_set_table (curve, short_table, 2, NULL));

    /* the previous table is untouched */
    g_assert_true (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 10);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 2), ==, 20);
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 3), ==, 30);
}

static void
test_level_cap_one (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (1);
    guint level = 1;
    guint64 xp = 0;
    guint64 pool = 10;

    /* the only valid table is empty */
    g_assert_true (lrg_experience_curve_set_table (curve, NULL, 0, NULL));
    g_assert_true (lrg_experience_curve_has_table (curve));
    g_assert_cmpuint (lrg_experience_curve_get_xp_for_level (curve, 1), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_get_total_xp_for_level (curve, 1), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 1000, &pool), ==, 0);
    g_assert_cmpuint (level, ==, 1);
    g_assert_cmpuint (xp, ==, 0);
    g_assert_cmpuint (pool, ==, 10);
    g_assert_true (lrg_experience_curve_validate (curve, 1, 0, NULL));
}

/* ========================================================================== */
/*                                  Awards                                    */
/* ========================================================================== */

static LrgExperienceCurve *
make_table_curve (void)
{
    LrgExperienceCurve *curve = lrg_experience_curve_new (4);
    const guint64 table[] = { 100, 200, 300 };

    g_assert_true (lrg_experience_curve_set_table (curve, table, 3, NULL));
    return curve;
}

static void
test_award_within_level (void)
{
    g_autoptr(LrgExperienceCurve) curve = make_table_curve ();
    guint level = 1;
    guint64 xp = 0;

    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 99, NULL), ==, 0);
    g_assert_cmpuint (level, ==, 1);
    g_assert_cmpuint (xp, ==, 99);

    /* exactly reaching the requirement levels up with zero progress */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 1, NULL), ==, 1);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (xp, ==, 0);

    /* zero award changes nothing */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 0, NULL), ==, 0);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (xp, ==, 0);
}

static void
test_award_many_levels (void)
{
    g_autoptr(LrgExperienceCurve) curve = make_table_curve ();
    guint level = 1;
    guint64 xp = 0;

    /* 100 + 200 = 300 levels twice, 299 left over in level 3 */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 599, NULL), ==, 2);
    g_assert_cmpuint (level, ==, 3);
    g_assert_cmpuint (xp, ==, 299);

    /* one more point reaches the cap */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 1, NULL), ==, 1);
    g_assert_cmpuint (level, ==, 4);
    g_assert_cmpuint (xp, ==, 0);

    /* awarding at the cap is a no-op */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 5000, NULL), ==, 0);
    g_assert_cmpuint (level, ==, 4);
    g_assert_cmpuint (xp, ==, 0);

    /* all levels at once with a large overflow is discarded at the cap */
    level = 1;
    xp = 50;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 1000000, NULL), ==, 3);
    g_assert_cmpuint (level, ==, 4);
    g_assert_cmpuint (xp, ==, 0);
}

static void
test_award_cap_minus_one (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (60);
    guint level = 59;
    guint64 need;
    guint64 xp = 0;

    need = lrg_experience_curve_get_xp_for_level (curve, 59);
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, need - 1, NULL), ==, 0);
    g_assert_cmpuint (level, ==, 59);
    g_assert_cmpuint (xp, ==, need - 1);
    g_assert_true (lrg_experience_curve_validate (curve, level, xp, NULL));

    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 1, NULL), ==, 1);
    g_assert_cmpuint (level, ==, 60);
    g_assert_cmpuint (xp, ==, 0);
    g_assert_true (lrg_experience_curve_validate (curve, level, xp, NULL));
}

static void
test_award_normalises_level (void)
{
    g_autoptr(LrgExperienceCurve) curve = make_table_curve ();
    guint level;
    guint64 xp;
    guint64 pool;

    /* level 0 is treated as level 1 */
    level = 0;
    xp = 0;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 10, NULL), ==, 0);
    g_assert_cmpuint (level, ==, 1);
    g_assert_cmpuint (xp, ==, 10);

    /* above the cap snaps to the cap, clears progress and leaves the pool */
    level = 90;
    xp = 77;
    pool = 33;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 10, &pool), ==, 0);
    g_assert_cmpuint (level, ==, 4);
    g_assert_cmpuint (xp, ==, 0);
    g_assert_cmpuint (pool, ==, 33);

    /* progress at or above the requirement is repaired by levelling */
    level = 1;
    xp = 1000;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 0, NULL), ==, 1);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (xp, ==, 0);
}

static void
test_award_saturates (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (3);
    const guint64 table[] = { G_MAXUINT64, G_MAXUINT64 };
    guint level = 1;
    guint64 xp = G_MAXUINT64 - 1;
    guint64 pool = G_MAXUINT64;

    g_assert_true (lrg_experience_curve_set_table (curve, table, 2, NULL));

    /* 1 point finishes level 1; G_MAXUINT64 - 1 carries into level 2 */
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, G_MAXUINT64, NULL), ==, 1);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (xp, ==, G_MAXUINT64 - 1);

    /* award + rested bonus saturates instead of wrapping */
    level = 1;
    xp = 0;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, G_MAXUINT64, &pool), ==, 1);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (pool, ==, 0);
    g_assert_cmpuint (xp, ==, 0);

    /* default formula curve accepts the largest award and ends at the cap */
    {
        g_autoptr(LrgExperienceCurve) big = lrg_experience_curve_new (1000);

        level = 1;
        xp = 0;
        g_assert_cmpuint (lrg_experience_curve_award (big, &level, &xp, G_MAXUINT64, NULL), ==, 999);
        g_assert_cmpuint (level, ==, 1000);
        g_assert_cmpuint (xp, ==, 0);
    }
}

/* ========================================================================== */
/*                               Rested experience                            */
/* ========================================================================== */

static void
test_rested_consumption (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (60);
    guint level;
    guint64 xp;
    guint64 pool;

    lrg_experience_curve_set_formula (curve, 100000.0, 0.0, 0.0, 0.0);

    /* pool smaller than the bonus: all of it is used */
    level = 1; xp = 0; pool = 50;
    lrg_experience_curve_award (curve, &level, &xp, 100, &pool);
    g_assert_cmpuint (xp, ==, 150);
    g_assert_cmpuint (pool, ==, 0);

    /* pool larger than the bonus: bonus equals amount * (2 - 1) */
    level = 1; xp = 0; pool = 500;
    lrg_experience_curve_award (curve, &level, &xp, 100, &pool);
    g_assert_cmpuint (xp, ==, 200);
    g_assert_cmpuint (pool, ==, 400);

    /* a 1.5 multiplier grants half the award as bonus */
    lrg_experience_curve_set_rested_multiplier (curve, 1.5);
    level = 1; xp = 0; pool = 500;
    lrg_experience_curve_award (curve, &level, &xp, 100, &pool);
    g_assert_cmpuint (xp, ==, 150);
    g_assert_cmpuint (pool, ==, 450);

    /* multiplier 1.0 grants nothing and keeps the pool */
    lrg_experience_curve_set_rested_multiplier (curve, 1.0);
    level = 1; xp = 0; pool = 500;
    lrg_experience_curve_award (curve, &level, &xp, 100, &pool);
    g_assert_cmpuint (xp, ==, 100);
    g_assert_cmpuint (pool, ==, 500);

    /* zero award consumes nothing */
    lrg_experience_curve_set_rested_multiplier (curve, 2.0);
    level = 1; xp = 0; pool = 500;
    lrg_experience_curve_award (curve, &level, &xp, 0, &pool);
    g_assert_cmpuint (xp, ==, 0);
    g_assert_cmpuint (pool, ==, 500);

    /* bonus can cause the level-up */
    level = 1; xp = 99900; pool = 100;
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 25, &pool), ==, 0);
    g_assert_cmpuint (xp, ==, 99950);
    g_assert_cmpuint (pool, ==, 75);
    g_assert_cmpuint (lrg_experience_curve_award (curve, &level, &xp, 25, &pool), ==, 1);
    g_assert_cmpuint (level, ==, 2);
    g_assert_cmpuint (xp, ==, 0);
    g_assert_cmpuint (pool, ==, 50);
}

static void
test_rested_accrual (void)
{
    g_autoptr(LrgExperienceCurve) curve = lrg_experience_curve_new (60);
    const gint64 hour = 3600;

    /* 8000 xp per level; 5% per 8h in a rest area */
    lrg_experience_curve_set_formula (curve, 8000.0, 0.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_experience_curve_get_rested_limit (curve, 1), ==, 12000);

    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, 8 * hour, TRUE), ==, 400);
    /* a quarter of the rate outside a rest area */
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, 8 * hour, FALSE), ==, 100);
    /* accrual adds to an existing pool */
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 1000, 8 * hour, TRUE), ==, 1400);

    /* zero or negative time accrues nothing */
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 77, 0, TRUE), ==, 77);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 77, -hour, TRUE), ==, 77);

    /* capped at 1.5 levels */
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, 1000 * hour, TRUE), ==, 12000);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 11990, 8 * hour, TRUE), ==, 12000);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, G_MAXINT64, TRUE), ==, 12000);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, G_MAXINT64, FALSE), ==, 12000);

    /* an existing pool above the cap is never reduced */
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 20000, 8 * hour, TRUE), ==, 20000);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, G_MAXUINT64, hour, TRUE), ==, G_MAXUINT64);

    /* at the level cap no rested experience accrues */
    g_assert_cmpuint (lrg_experience_curve_get_rested_limit (curve, 60), ==, 0);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 60, 5, 8 * hour, TRUE), ==, 5);

    /* rate 0 disables accrual */
    lrg_experience_curve_set_rested_rate (curve, 0.0);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, 8 * hour, TRUE), ==, 0);

    /* cap 0 disables it too */
    lrg_experience_curve_set_rested_rate (curve, 0.05 / 8.0);
    lrg_experience_curve_set_rested_cap (curve, 0.0);
    g_assert_cmpuint (lrg_experience_curve_accrue_rested (curve, 1, 0, 8 * hour, TRUE), ==, 0);
}

/* ========================================================================== */
/*                                 Validation                                 */
/* ========================================================================== */

static void
test_validate (void)
{
    g_autoptr(LrgExperienceCurve) curve = make_table_curve ();
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_experience_curve_validate (curve, 1, 0, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_experience_curve_validate (curve, 1, 99, NULL));
    g_assert_true (lrg_experience_curve_validate (curve, 3, 299, NULL));
    g_assert_true (lrg_experience_curve_validate (curve, 4, 0, NULL));

    g_assert_false (lrg_experience_curve_validate (curve, 0, 0, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_validate (curve, 5, 0, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_validate (curve, 1, 100, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_validate (curve, 4, 1, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_experience_curve_validate (curve, 2, G_MAXUINT64, NULL));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/experience-curve/defaults", test_defaults);
    g_test_add_func ("/experience-curve/properties", test_properties);
    g_test_add_func ("/experience-curve/notify-only-on-change", test_notify_only_on_change);
    g_test_add_func ("/experience-curve/setter-rejections", test_setter_rejections);
    g_test_add_func ("/experience-curve/formula/levels", test_formula_levels);
    g_test_add_func ("/experience-curve/formula/rounding-minimum", test_formula_rounding_and_minimum);
    g_test_add_func ("/experience-curve/formula/saturates", test_formula_saturates);
    g_test_add_func ("/experience-curve/table/overrides-formula", test_table_overrides_formula);
    g_test_add_func ("/experience-curve/table/rejections", test_table_rejections);
    g_test_add_func ("/experience-curve/table/level-cap-one", test_level_cap_one);
    g_test_add_func ("/experience-curve/award/within-level", test_award_within_level);
    g_test_add_func ("/experience-curve/award/many-levels", test_award_many_levels);
    g_test_add_func ("/experience-curve/award/cap-minus-one", test_award_cap_minus_one);
    g_test_add_func ("/experience-curve/award/normalises-level", test_award_normalises_level);
    g_test_add_func ("/experience-curve/award/saturates", test_award_saturates);
    g_test_add_func ("/experience-curve/rested/consumption", test_rested_consumption);
    g_test_add_func ("/experience-curve/rested/accrual", test_rested_accrual);
    g_test_add_func ("/experience-curve/validate", test_validate);

    return g_test_run ();
}
