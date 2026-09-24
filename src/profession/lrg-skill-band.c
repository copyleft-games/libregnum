/* lrg-skill-band.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Skill-up colour bands (orange/yellow/green/grey) and the shared
 * identifier validator of the profession module.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "profession/lrg-skill-band.h"
#include "profession/lrg-profession-private.h"

#include <string.h>

G_DEFINE_BOXED_TYPE (LrgSkillBand, lrg_skill_band,
                     lrg_skill_band_copy,
                     lrg_skill_band_free)

gboolean
_lrg_profession_id_valid (const gchar *id)
{
    gsize len;

    /* Reject NULL and empty identifiers outright */
    if (id == NULL || id[0] == '\0')
        return FALSE;

    /* Bound the scan so hostile unterminated-looking data stays cheap */
    len = strnlen (id, LRG_PROFESSION_ID_MAX_BYTES + 1);
    if (len > LRG_PROFESSION_ID_MAX_BYTES)
        return FALSE;

    return g_utf8_validate (id, (gssize)len, NULL);
}

LrgSkillBand *
lrg_skill_band_new (guint orange,
                    guint yellow,
                    guint green,
                    guint grey)
{
    LrgSkillBand *self;

    self = g_new0 (LrgSkillBand, 1);
    self->orange = orange;
    self->yellow = yellow;
    self->green = green;
    self->grey = grey;
    return self;
}

/*
 * saturating_offset:
 * Adds @offset to @base, saturating at LRG_PROFESSION_SKILL_LIMIT.
 */
static guint
saturating_offset (guint base,
                   guint offset)
{
    if (base >= LRG_PROFESSION_SKILL_LIMIT ||
        offset >= (guint)LRG_PROFESSION_SKILL_LIMIT - base)
        return LRG_PROFESSION_SKILL_LIMIT;
    return base + offset;
}

LrgSkillBand *
lrg_skill_band_new_default (guint required_skill)
{
    guint orange;

    /* Clamp the base first so the band stays valid for any input */
    orange = MIN (required_skill, (guint)LRG_PROFESSION_SKILL_LIMIT);
    return lrg_skill_band_new (orange,
                               saturating_offset (orange, LRG_SKILL_BAND_DEFAULT_YELLOW_OFFSET),
                               saturating_offset (orange, LRG_SKILL_BAND_DEFAULT_GREEN_OFFSET),
                               saturating_offset (orange, LRG_SKILL_BAND_DEFAULT_GREY_OFFSET));
}

LrgSkillBand *
lrg_skill_band_copy (const LrgSkillBand *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_skill_band_new (self->orange, self->yellow,
                               self->green, self->grey);
}

void
lrg_skill_band_free (LrgSkillBand *self)
{
    g_free (self);
}

gboolean
lrg_skill_band_is_valid (const LrgSkillBand *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    /* Thresholds must be ordered and within the module-wide skill limit */
    return self->orange <= self->yellow &&
           self->yellow <= self->green &&
           self->green <= self->grey &&
           self->grey <= LRG_PROFESSION_SKILL_LIMIT;
}

LrgSkillDifficulty
lrg_skill_band_get_difficulty (const LrgSkillBand *self,
                               guint               skill)
{
    g_return_val_if_fail (self != NULL, LRG_SKILL_DIFFICULTY_UNAVAILABLE);

    /* A malformed band never offers anything */
    if (!lrg_skill_band_is_valid (self))
        return LRG_SKILL_DIFFICULTY_UNAVAILABLE;

    /* Each threshold is the first skill of its colour */
    if (skill < self->orange)
        return LRG_SKILL_DIFFICULTY_UNAVAILABLE;
    if (skill < self->yellow)
        return LRG_SKILL_DIFFICULTY_ORANGE;
    if (skill < self->green)
        return LRG_SKILL_DIFFICULTY_YELLOW;
    if (skill < self->grey)
        return LRG_SKILL_DIFFICULTY_GREEN;
    return LRG_SKILL_DIFFICULTY_GREY;
}

gdouble
lrg_skill_band_get_chance (const LrgSkillBand *self,
                           guint               skill)
{
    LrgSkillDifficulty difficulty;
    gdouble chance;

    g_return_val_if_fail (self != NULL, 0.0);

    difficulty = lrg_skill_band_get_difficulty (self, skill);
    switch (difficulty)
    {
    case LRG_SKILL_DIFFICULTY_ORANGE:
        return 1.0;
    case LRG_SKILL_DIFFICULTY_YELLOW:
    case LRG_SKILL_DIFFICULTY_GREEN:
        /*
         * YELLOW/GREEN imply yellow <= skill < grey, so grey > yellow and
         * the divisor is positive. Clamp anyway for belt and braces.
         */
        chance = (gdouble)(self->grey - skill) /
                 (gdouble)(self->grey - self->yellow);
        return CLAMP (chance, 0.0, 1.0);
    case LRG_SKILL_DIFFICULTY_UNAVAILABLE:
    case LRG_SKILL_DIFFICULTY_GREY:
    default:
        return 0.0;
    }
}
