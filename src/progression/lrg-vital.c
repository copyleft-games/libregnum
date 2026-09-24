/* lrg-vital.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Resource pool with combat/idle regeneration and clamped arithmetic.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-vital.h"

#include <math.h>
#include <string.h>

#define LRG_VITAL_MAX_KIND_LENGTH (128)

G_DEFINE_BOXED_TYPE (LrgVital, lrg_vital, lrg_vital_copy, lrg_vital_free)

static gboolean
kind_is_valid (const gchar *kind)
{
    gsize len;

    if (kind == NULL)
        return FALSE;
    len = strlen (kind);
    if (len == 0 || len > LRG_VITAL_MAX_KIND_LENGTH)
        return FALSE;
    return g_utf8_validate (kind, (gssize) len, NULL);
}

static gboolean
maximum_is_valid (gdouble maximum)
{
    return isfinite (maximum) && maximum > 0.0 && maximum <= LRG_VITAL_MAX_VALUE;
}

static gboolean
rate_is_valid (gdouble rate)
{
    return isfinite (rate) && fabs (rate) <= LRG_VITAL_MAX_VALUE;
}

LrgVital *
lrg_vital_new (const gchar *kind,
               gdouble      maximum,
               gdouble      regen,
               gdouble      idle_regen)
{
    LrgVital *self;

    g_return_val_if_fail (kind_is_valid (kind), NULL);
    g_return_val_if_fail (maximum_is_valid (maximum), NULL);
    g_return_val_if_fail (rate_is_valid (regen), NULL);
    g_return_val_if_fail (rate_is_valid (idle_regen), NULL);

    self = g_new0 (LrgVital, 1);
    self->kind = g_strdup (kind);
    self->maximum = maximum;
    self->regen = regen;
    self->idle_regen = idle_regen;
    self->current = (idle_regen >= 0.0) ? maximum : 0.0;
    return self;
}

LrgVital *
lrg_vital_copy (const LrgVital *self)
{
    LrgVital *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgVital, 1);
    *copy = *self;
    copy->kind = g_strdup (self->kind);
    return copy;
}

void
lrg_vital_free (LrgVital *self)
{
    if (self == NULL)
        return;

    g_free (self->kind);
    g_free (self);
}

/* Clamps current to 0..maximum. */
static void
clamp_current (LrgVital *self)
{
    if (!(self->current > 0.0))
        self->current = 0.0;
    else if (self->current > self->maximum)
        self->current = self->maximum;
}

void
lrg_vital_tick (LrgVital *self,
                gdouble   delta,
                gboolean  in_combat)
{
    gdouble rate;

    g_return_if_fail (self != NULL);

    if (!isfinite (delta) || !(delta > 0.0))
        return;

    rate = in_combat ? self->regen : self->idle_regen;
    self->current += rate * delta;
    clamp_current (self);
}

gboolean
lrg_vital_spend (LrgVital *self,
                 gdouble   amount)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!isfinite (amount) || amount < 0.0)
        return FALSE;
    if (amount > self->current)
        return FALSE;

    self->current -= amount;
    clamp_current (self);
    return TRUE;
}

void
lrg_vital_gain (LrgVital *self,
                gdouble   amount)
{
    g_return_if_fail (self != NULL);

    if (!isfinite (amount))
        return;
    self->current += amount;
    clamp_current (self);
}

void
lrg_vital_set_maximum (LrgVital *self,
                       gdouble   maximum,
                       gboolean  keep_ratio)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (maximum_is_valid (maximum));

    if (keep_ratio)
        self->current = lrg_vital_get_fraction (self) * maximum;
    self->maximum = maximum;
    clamp_current (self);
}

gdouble
lrg_vital_get_fraction (const LrgVital *self)
{
    g_return_val_if_fail (self != NULL, 0.0);

    if (!(self->maximum > 0.0))
        return 0.0;
    return CLAMP (self->current / self->maximum, 0.0, 1.0);
}

GVariant *
lrg_vital_to_variant (const LrgVital *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return g_variant_ref_sink (g_variant_new ("(sdddd)",
                                              self->kind != NULL ? self->kind : "",
                                              self->current, self->maximum,
                                              self->regen, self->idle_regen));
}

LrgVital *
lrg_vital_new_from_variant (GVariant  *variant,
                            GError   **error)
{
    LrgVital *self;
    const gchar *kind;
    gdouble current;
    gdouble maximum;
    gdouble regen;
    gdouble idle_regen;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_VITAL_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "vital variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant), LRG_VITAL_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "vital variant is not in normal form");
        return NULL;
    }

    g_variant_get (variant, "(&sdddd)", &kind, &current, &maximum, &regen, &idle_regen);

    if (!kind_is_valid (kind))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "vital variant has an invalid kind");
        return NULL;
    }
    if (!maximum_is_valid (maximum))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "vital '%s' has an invalid maximum", kind);
        return NULL;
    }
    if (!isfinite (current) || current < 0.0 || current > maximum)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "vital '%s' current value is outside 0..maximum", kind);
        return NULL;
    }
    if (!rate_is_valid (regen) || !rate_is_valid (idle_regen))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "vital '%s' has an invalid regeneration rate", kind);
        return NULL;
    }

    self = g_new0 (LrgVital, 1);
    self->kind = g_strdup (kind);
    self->current = current;
    self->maximum = maximum;
    self->regen = regen;
    self->idle_regen = idle_regen;
    return self;
}
