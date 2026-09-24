/* lrg-experience-curve.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Level curve with a polynomial formula or an explicit table, saturating
 * awards and rested experience accrual. All time is supplied by the caller.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-experience-curve.h"

#include <math.h>

/* 2^64 as a double: every finite double below this fits in a guint64. */
#define LRG_XP_TWO_POW_64 (18446744073709551616.0)

#define LRG_XP_DEFAULT_LEVEL_CAP         (60)
#define LRG_XP_DEFAULT_RESTED_RATE       (0.05 / 8.0)
#define LRG_XP_DEFAULT_RESTED_CAP        (1.5)
#define LRG_XP_DEFAULT_RESTED_MULTIPLIER (2.0)
#define LRG_XP_MAX_RESTED_RATE           (10.0)
#define LRG_XP_MAX_RESTED_CAP            (100.0)
#define LRG_XP_MAX_RESTED_MULTIPLIER     (100.0)

struct _LrgExperienceCurve
{
    GObject  parent_instance;

    guint    level_cap;
    gdouble  rested_rate;
    gdouble  rested_cap;
    gdouble  rested_multiplier;

    /* polynomial coefficients used when table == NULL */
    gdouble  base;
    gdouble  linear;
    gdouble  quadratic;
    gdouble  cubic;

    /* optional explicit table: level_cap - 1 guint64 entries */
    GArray  *table;
};

G_DEFINE_TYPE (LrgExperienceCurve, lrg_experience_curve, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_LEVEL_CAP,
    PROP_RESTED_RATE,
    PROP_RESTED_CAP,
    PROP_RESTED_MULTIPLIER,
    PROP_FORMULA_BASE,
    PROP_FORMULA_LINEAR,
    PROP_FORMULA_QUADRATIC,
    PROP_FORMULA_CUBIC,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * round_to_u64:
 *
 * Rounds a double to the nearest guint64, saturating at both ends. NaN and
 * values at or below zero become 0.
 */
static guint64
round_to_u64 (gdouble value)
{
    gdouble rounded;

    if (!(value > 0.0))
        return 0;
    rounded = floor (value + 0.5);
    if (rounded >= LRG_XP_TWO_POW_64)
        return G_MAXUINT64;
    return (guint64) rounded;
}

/* Saturating unsigned 64-bit addition. */
static guint64
sat_add (guint64 a,
         guint64 b)
{
    if (G_MAXUINT64 - a < b)
        return G_MAXUINT64;
    return a + b;
}

static void
lrg_experience_curve_finalize (GObject *object)
{
    LrgExperienceCurve *self = LRG_EXPERIENCE_CURVE (object);

    g_clear_pointer (&self->table, g_array_unref);

    G_OBJECT_CLASS (lrg_experience_curve_parent_class)->finalize (object);
}

/*
 * set_coefficient:
 *
 * Shared setter for the four formula properties. A formula change always
 * drops an installed table so the curve is never a mix of both.
 */
static void
set_coefficient (LrgExperienceCurve *self,
                 gdouble            *slot,
                 gdouble             value,
                 guint               prop)
{
    g_return_if_fail (isfinite (value));

    if (self->table != NULL)
        g_clear_pointer (&self->table, g_array_unref);
    if (*slot != value)
    {
        *slot = value;
        g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
    }
}

static void
lrg_experience_curve_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgExperienceCurve *self = LRG_EXPERIENCE_CURVE (object);

    switch (prop_id)
    {
    case PROP_LEVEL_CAP:
        g_value_set_uint (value, self->level_cap);
        break;
    case PROP_RESTED_RATE:
        g_value_set_double (value, self->rested_rate);
        break;
    case PROP_RESTED_CAP:
        g_value_set_double (value, self->rested_cap);
        break;
    case PROP_RESTED_MULTIPLIER:
        g_value_set_double (value, self->rested_multiplier);
        break;
    case PROP_FORMULA_BASE:
        g_value_set_double (value, self->base);
        break;
    case PROP_FORMULA_LINEAR:
        g_value_set_double (value, self->linear);
        break;
    case PROP_FORMULA_QUADRATIC:
        g_value_set_double (value, self->quadratic);
        break;
    case PROP_FORMULA_CUBIC:
        g_value_set_double (value, self->cubic);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_experience_curve_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgExperienceCurve *self = LRG_EXPERIENCE_CURVE (object);

    switch (prop_id)
    {
    case PROP_LEVEL_CAP:
        lrg_experience_curve_set_level_cap (self, g_value_get_uint (value));
        break;
    case PROP_RESTED_RATE:
        lrg_experience_curve_set_rested_rate (self, g_value_get_double (value));
        break;
    case PROP_RESTED_CAP:
        lrg_experience_curve_set_rested_cap (self, g_value_get_double (value));
        break;
    case PROP_RESTED_MULTIPLIER:
        lrg_experience_curve_set_rested_multiplier (self, g_value_get_double (value));
        break;
    case PROP_FORMULA_BASE:
        set_coefficient (self, &self->base, g_value_get_double (value), PROP_FORMULA_BASE);
        break;
    case PROP_FORMULA_LINEAR:
        set_coefficient (self, &self->linear, g_value_get_double (value), PROP_FORMULA_LINEAR);
        break;
    case PROP_FORMULA_QUADRATIC:
        set_coefficient (self, &self->quadratic, g_value_get_double (value), PROP_FORMULA_QUADRATIC);
        break;
    case PROP_FORMULA_CUBIC:
        set_coefficient (self, &self->cubic, g_value_get_double (value), PROP_FORMULA_CUBIC);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_experience_curve_class_init (LrgExperienceCurveClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_experience_curve_finalize;
    object_class->get_property = lrg_experience_curve_get_property;
    object_class->set_property = lrg_experience_curve_set_property;

    /**
     * LrgExperienceCurve:level-cap:
     *
     * Highest reachable level.
     */
    properties[PROP_LEVEL_CAP] =
        g_param_spec_uint ("level-cap", "Level Cap", "Highest reachable level",
                           1, LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP,
                           LRG_XP_DEFAULT_LEVEL_CAP,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                           G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:rested-rate:
     *
     * Fraction of a level's requirement accrued per hour inside a rest area.
     */
    properties[PROP_RESTED_RATE] =
        g_param_spec_double ("rested-rate", "Rested Rate",
                             "Fraction of a level accrued per hour of rest",
                             0.0, LRG_XP_MAX_RESTED_RATE, LRG_XP_DEFAULT_RESTED_RATE,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:rested-cap:
     *
     * Rested pool cap as a fraction of a level's requirement.
     */
    properties[PROP_RESTED_CAP] =
        g_param_spec_double ("rested-cap", "Rested Cap",
                             "Rested pool cap as a fraction of a level",
                             0.0, LRG_XP_MAX_RESTED_CAP, LRG_XP_DEFAULT_RESTED_CAP,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:rested-multiplier:
     *
     * Award multiplier while rested experience remains.
     */
    properties[PROP_RESTED_MULTIPLIER] =
        g_param_spec_double ("rested-multiplier", "Rested Multiplier",
                             "Award multiplier while rested",
                             1.0, LRG_XP_MAX_RESTED_MULTIPLIER,
                             LRG_XP_DEFAULT_RESTED_MULTIPLIER,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:formula-base:
     *
     * Constant term of the level formula.
     */
    properties[PROP_FORMULA_BASE] =
        g_param_spec_double ("formula-base", "Formula Base", "Constant term",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:formula-linear:
     *
     * Coefficient of L in the level formula.
     */
    properties[PROP_FORMULA_LINEAR] =
        g_param_spec_double ("formula-linear", "Formula Linear", "Coefficient of L",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:formula-quadratic:
     *
     * Coefficient of L squared in the level formula.
     */
    properties[PROP_FORMULA_QUADRATIC] =
        g_param_spec_double ("formula-quadratic", "Formula Quadratic",
                             "Coefficient of L squared",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 10.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgExperienceCurve:formula-cubic:
     *
     * Coefficient of L cubed in the level formula.
     */
    properties[PROP_FORMULA_CUBIC] =
        g_param_spec_double ("formula-cubic", "Formula Cubic", "Coefficient of L cubed",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_experience_curve_init (LrgExperienceCurve *self)
{
    self->level_cap = LRG_XP_DEFAULT_LEVEL_CAP;
    self->rested_rate = LRG_XP_DEFAULT_RESTED_RATE;
    self->rested_cap = LRG_XP_DEFAULT_RESTED_CAP;
    self->rested_multiplier = LRG_XP_DEFAULT_RESTED_MULTIPLIER;
    self->base = 100.0;
    self->linear = 100.0;
    self->quadratic = 10.0;
    self->cubic = 0.0;
    self->table = NULL;
}

LrgExperienceCurve *
lrg_experience_curve_new (guint level_cap)
{
    g_return_val_if_fail (level_cap >= 1 && level_cap <= LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP, NULL);

    return g_object_new (LRG_TYPE_EXPERIENCE_CURVE, "level-cap", level_cap, NULL);
}

guint
lrg_experience_curve_get_level_cap (LrgExperienceCurve *self)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 1);

    return self->level_cap;
}

void
lrg_experience_curve_set_level_cap (LrgExperienceCurve *self,
                                    guint               level_cap)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));
    g_return_if_fail (level_cap >= 1 && level_cap <= LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP);

    if (self->level_cap == level_cap)
        return;

    /* a table is sized for exactly one cap */
    g_clear_pointer (&self->table, g_array_unref);
    self->level_cap = level_cap;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEVEL_CAP]);
}

gdouble
lrg_experience_curve_get_rested_rate (LrgExperienceCurve *self)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0.0);

    return self->rested_rate;
}

void
lrg_experience_curve_set_rested_rate (LrgExperienceCurve *self,
                                      gdouble             rate)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));
    g_return_if_fail (isfinite (rate) && rate >= 0.0 && rate <= LRG_XP_MAX_RESTED_RATE);

    if (self->rested_rate == rate)
        return;
    self->rested_rate = rate;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTED_RATE]);
}

gdouble
lrg_experience_curve_get_rested_cap (LrgExperienceCurve *self)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0.0);

    return self->rested_cap;
}

void
lrg_experience_curve_set_rested_cap (LrgExperienceCurve *self,
                                     gdouble             cap)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));
    g_return_if_fail (isfinite (cap) && cap >= 0.0 && cap <= LRG_XP_MAX_RESTED_CAP);

    if (self->rested_cap == cap)
        return;
    self->rested_cap = cap;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTED_CAP]);
}

gdouble
lrg_experience_curve_get_rested_multiplier (LrgExperienceCurve *self)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 1.0);

    return self->rested_multiplier;
}

void
lrg_experience_curve_set_rested_multiplier (LrgExperienceCurve *self,
                                            gdouble             multiplier)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));
    g_return_if_fail (isfinite (multiplier) && multiplier >= 1.0 &&
                      multiplier <= LRG_XP_MAX_RESTED_MULTIPLIER);

    if (self->rested_multiplier == multiplier)
        return;
    self->rested_multiplier = multiplier;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTED_MULTIPLIER]);
}

void
lrg_experience_curve_set_formula (LrgExperienceCurve *self,
                                  gdouble             base,
                                  gdouble             linear,
                                  gdouble             quadratic,
                                  gdouble             cubic)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));
    g_return_if_fail (isfinite (base) && isfinite (linear) &&
                      isfinite (quadratic) && isfinite (cubic));

    g_object_freeze_notify (G_OBJECT (self));
    set_coefficient (self, &self->base, base, PROP_FORMULA_BASE);
    set_coefficient (self, &self->linear, linear, PROP_FORMULA_LINEAR);
    set_coefficient (self, &self->quadratic, quadratic, PROP_FORMULA_QUADRATIC);
    set_coefficient (self, &self->cubic, cubic, PROP_FORMULA_CUBIC);
    g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_experience_curve_get_formula (LrgExperienceCurve *self,
                                  gdouble            *base,
                                  gdouble            *linear,
                                  gdouble            *quadratic,
                                  gdouble            *cubic)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));

    if (base != NULL)
        *base = self->base;
    if (linear != NULL)
        *linear = self->linear;
    if (quadratic != NULL)
        *quadratic = self->quadratic;
    if (cubic != NULL)
        *cubic = self->cubic;
}

gboolean
lrg_experience_curve_set_table (LrgExperienceCurve *self,
                                const guint64      *table,
                                gsize               n_levels,
                                GError            **error)
{
    GArray *copy;
    gsize i;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* validate everything before touching the curve */
    if (n_levels != (gsize) self->level_cap - 1)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "experience table has %" G_GSIZE_FORMAT " entries, level cap %u needs %u",
                     n_levels, self->level_cap, self->level_cap - 1);
        return FALSE;
    }
    if (n_levels > 0 && table == NULL)
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "experience table is NULL");
        return FALSE;
    }
    for (i = 0; i < n_levels; i++)
    {
        if (table[i] == 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "experience table entry for level %" G_GSIZE_FORMAT " is zero",
                         i + 1);
            return FALSE;
        }
    }

    copy = g_array_sized_new (FALSE, FALSE, sizeof (guint64), (guint) n_levels);
    if (n_levels > 0)
        g_array_append_vals (copy, table, (guint) n_levels);
    g_clear_pointer (&self->table, g_array_unref);
    self->table = copy;
    return TRUE;
}

void
lrg_experience_curve_clear_table (LrgExperienceCurve *self)
{
    g_return_if_fail (LRG_IS_EXPERIENCE_CURVE (self));

    g_clear_pointer (&self->table, g_array_unref);
}

gboolean
lrg_experience_curve_has_table (LrgExperienceCurve *self)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), FALSE);

    return self->table != NULL;
}

guint64
lrg_experience_curve_get_xp_for_level (LrgExperienceCurve *self,
                                       guint               level)
{
    gdouble l;
    gdouble value;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0);

    if (level == 0 || level >= self->level_cap)
        return 0;

    if (self->table != NULL)
        return g_array_index (self->table, guint64, level - 1);

    /* polynomial, minimum 1 (NaN and negative results also become 1) */
    l = (gdouble) level;
    value = self->base + self->linear * l + self->quadratic * l * l +
            self->cubic * l * l * l;
    if (!(value >= 1.0))
        return 1;
    return round_to_u64 (value);
}

guint64
lrg_experience_curve_get_total_xp_for_level (LrgExperienceCurve *self,
                                             guint               level)
{
    guint64 total;
    guint l;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0);

    if (level > self->level_cap)
        level = self->level_cap;

    total = 0;
    for (l = 1; l < level; l++)
        total = sat_add (total, lrg_experience_curve_get_xp_for_level (self, l));
    return total;
}

guint
lrg_experience_curve_award (LrgExperienceCurve *self,
                            guint              *level,
                            guint64            *xp,
                            guint64             amount,
                            guint64            *rested_pool)
{
    guint64 total;
    guint gained;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0);
    g_return_val_if_fail (level != NULL, 0);
    g_return_val_if_fail (xp != NULL, 0);

    /* normalise the incoming level into the curve's range */
    if (*level < 1)
        *level = 1;
    if (*level >= self->level_cap)
    {
        *level = self->level_cap;
        *xp = 0;
        return 0;
    }

    /* rested bonus: MIN (pool, amount * (multiplier - 1)) */
    total = amount;
    if (rested_pool != NULL && *rested_pool > 0 && amount > 0)
    {
        guint64 bonus;

        bonus = round_to_u64 ((gdouble) amount * (self->rested_multiplier - 1.0));
        bonus = MIN (bonus, *rested_pool);
        *rested_pool -= bonus;
        total = sat_add (total, bonus);
    }

    /* spend the award level by level; no intermediate sum can overflow
     * because we only ever compare against the remainder */
    gained = 0;
    while (*level < self->level_cap)
    {
        guint64 need;
        guint64 missing;

        need = lrg_experience_curve_get_xp_for_level (self, *level);
        missing = (*xp >= need) ? 0 : need - *xp;
        if (total < missing)
        {
            *xp += total;
            return gained;
        }
        total -= missing;
        *xp = 0;
        (*level)++;
        gained++;
    }

    /* reached the cap: progress is always zero there */
    *xp = 0;
    return gained;
}

guint64
lrg_experience_curve_get_rested_limit (LrgExperienceCurve *self,
                                       guint               level)
{
    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), 0);

    return round_to_u64 (self->rested_cap *
                         (gdouble) lrg_experience_curve_get_xp_for_level (self, level));
}

guint64
lrg_experience_curve_accrue_rested (LrgExperienceCurve *self,
                                    guint               level,
                                    guint64             pool,
                                    gint64              seconds,
                                    gboolean            in_rest_area)
{
    guint64 limit;
    guint64 gain;
    gdouble factor;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), pool);

    if (seconds <= 0)
        return pool;

    limit = lrg_experience_curve_get_rested_limit (self, level);
    if (pool >= limit)
        return pool;

    /* rate is per hour of rest; outside a rest area a quarter applies */
    factor = in_rest_area ? 1.0 : 0.25;
    gain = round_to_u64 (self->rested_rate * ((gdouble) seconds / 3600.0) *
                         (gdouble) lrg_experience_curve_get_xp_for_level (self, level) *
                         factor);
    return MIN (sat_add (pool, gain), limit);
}

gboolean
lrg_experience_curve_validate (LrgExperienceCurve *self,
                               guint               level,
                               guint64             xp,
                               GError            **error)
{
    guint64 need;

    g_return_val_if_fail (LRG_IS_EXPERIENCE_CURVE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (level < 1 || level > self->level_cap)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "level %u is outside 1..%u", level, self->level_cap);
        return FALSE;
    }
    if (level == self->level_cap)
    {
        if (xp != 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "experience %" G_GUINT64_FORMAT " must be 0 at the level cap", xp);
            return FALSE;
        }
        return TRUE;
    }

    need = lrg_experience_curve_get_xp_for_level (self, level);
    if (xp >= need)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "experience %" G_GUINT64_FORMAT " is not below %" G_GUINT64_FORMAT
                     " required at level %u", xp, need, level);
        return FALSE;
    }
    return TRUE;
}
