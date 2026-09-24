/* lrg-aura.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Boxed aura instance and its validation rules.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-aura.h"

#include <math.h>
#include <string.h>

#define LRG_AURA_MAX_ID_LENGTH (128)

G_DEFINE_BOXED_TYPE (LrgAura, lrg_aura, lrg_aura_copy, lrg_aura_free)

LrgAura *
lrg_aura_new (const gchar *id,
              LrgAuraKind  kind,
              gdouble      duration)
{
    LrgAura *self;

    g_return_val_if_fail (id != NULL && *id != '\0', NULL);
    g_return_val_if_fail (isfinite (duration) && duration >= 0.0, NULL);

    self = g_new0 (LrgAura, 1);
    self->id = g_strdup (id);
    self->kind = kind;
    self->duration = duration;
    self->remaining = duration;
    self->stacks = 1;
    self->max_stacks = 1;
    return self;
}

LrgAura *
lrg_aura_copy (const LrgAura *self)
{
    LrgAura *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgAura, 1);
    *copy = *self;
    copy->id = g_strdup (self->id);
    copy->dispel = g_strdup (self->dispel);
    copy->stat = g_strdup (self->stat);
    return copy;
}

void
lrg_aura_free (LrgAura *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->dispel);
    g_free (self->stat);
    g_free (self);
}

gdouble
lrg_aura_get_total (const LrgAura *self)
{
    g_return_val_if_fail (self != NULL, 0.0);

    return self->magnitude * (gdouble) self->stacks;
}

/* Keys are 1-128 bytes of UTF-8; optional keys may also be NULL. */
static gboolean
key_is_valid (const gchar *key,
              gboolean     optional)
{
    gsize len;

    if (key == NULL)
        return optional;
    len = strlen (key);
    if (len == 0 || len > LRG_AURA_MAX_ID_LENGTH)
        return FALSE;
    return g_utf8_validate (key, (gssize) len, NULL);
}

static gboolean
seconds_are_valid (gdouble seconds)
{
    return isfinite (seconds) && seconds >= 0.0 && seconds <= LRG_AURA_MAX_SECONDS;
}

gboolean
lrg_aura_is_valid (const LrgAura *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!key_is_valid (self->id, FALSE) ||
        !key_is_valid (self->dispel, TRUE) ||
        !key_is_valid (self->stat, TRUE))
        return FALSE;
    if (self->kind != LRG_AURA_KIND_BUFF && self->kind != LRG_AURA_KIND_DEBUFF)
        return FALSE;
    if (!isfinite (self->magnitude))
        return FALSE;
    if (!seconds_are_valid (self->duration) || !seconds_are_valid (self->period))
        return FALSE;
    if (!seconds_are_valid (self->remaining) || self->remaining > self->duration)
        return FALSE;
    if (!seconds_are_valid (self->tick_left) || self->tick_left > self->period)
        return FALSE;
    if (self->max_stacks < 1 || self->max_stacks > LRG_AURA_MAX_STACKS)
        return FALSE;
    if (self->stacks < 1 || self->stacks > self->max_stacks)
        return FALSE;
    return TRUE;
}
