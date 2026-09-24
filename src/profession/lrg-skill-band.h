/* lrg-skill-band.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkillBand - orange/yellow/green/grey skill-up colour thresholds.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_PROFESSION_SKILL_LIMIT:
 *
 * Largest skill value or skill cap accepted anywhere in the profession
 * module. Tier caps, recipe requirements and persisted snapshots above
 * this value are rejected.
 */
#define LRG_PROFESSION_SKILL_LIMIT (100000)

#define LRG_TYPE_SKILL_BAND (lrg_skill_band_get_type ())

/**
 * LrgSkillBand:
 * @orange: first skill at which the entry is usable; below it the entry is unavailable
 * @yellow: first skill at which the entry turns yellow
 * @green: first skill at which the entry turns green
 * @grey: first skill at which the entry turns grey (never grants a skill point)
 *
 * WoW-style skill-up thresholds for a recipe or gather node. A band is valid
 * when `orange <= yellow <= green <= grey`. Equal thresholds collapse the
 * colour between them.
 */
struct _LrgSkillBand
{
    guint orange;
    guint yellow;
    guint green;
    guint grey;
};

LRG_AVAILABLE_IN_ALL
GType lrg_skill_band_get_type (void) G_GNUC_CONST;

/**
 * lrg_skill_band_new:
 * @orange: orange threshold (usable from here)
 * @yellow: yellow threshold
 * @green: green threshold
 * @grey: grey threshold
 *
 * Creates a new skill band. The thresholds are stored as given; use
 * lrg_skill_band_is_valid() to check ordering.
 *
 * Returns: (transfer full): a new #LrgSkillBand
 */
LRG_AVAILABLE_IN_ALL
LrgSkillBand *lrg_skill_band_new (guint orange,
                                  guint yellow,
                                  guint green,
                                  guint grey);

/**
 * LRG_SKILL_BAND_DEFAULT_YELLOW_OFFSET:
 *
 * Skill points above the orange threshold at which a default band turns
 * yellow. See lrg_skill_band_new_default().
 */
#define LRG_SKILL_BAND_DEFAULT_YELLOW_OFFSET (15)

/**
 * LRG_SKILL_BAND_DEFAULT_GREEN_OFFSET:
 *
 * Skill points above the orange threshold at which a default band turns
 * green.
 */
#define LRG_SKILL_BAND_DEFAULT_GREEN_OFFSET (30)

/**
 * LRG_SKILL_BAND_DEFAULT_GREY_OFFSET:
 *
 * Skill points above the orange threshold at which a default band turns
 * grey.
 */
#define LRG_SKILL_BAND_DEFAULT_GREY_OFFSET (45)

/**
 * lrg_skill_band_new_default:
 * @required_skill: orange threshold (the entry's required skill)
 *
 * Creates the band used by recipes and gather nodes that never had an
 * explicit band: orange = @required_skill, then +15 yellow, +30 green and
 * +45 grey. Every threshold saturates at %LRG_PROFESSION_SKILL_LIMIT.
 *
 * Returns: (transfer full): a new valid #LrgSkillBand
 */
LRG_AVAILABLE_IN_ALL
LrgSkillBand *lrg_skill_band_new_default (guint required_skill);

/**
 * lrg_skill_band_copy:
 * @self: an #LrgSkillBand
 *
 * Copies a skill band.
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgSkillBand *lrg_skill_band_copy (const LrgSkillBand *self);

/**
 * lrg_skill_band_free:
 * @self: (nullable): an #LrgSkillBand
 *
 * Frees a skill band. %NULL is ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skill_band_free (LrgSkillBand *self);

/**
 * lrg_skill_band_is_valid:
 * @self: an #LrgSkillBand
 *
 * Checks that `orange <= yellow <= green <= grey` and that every threshold
 * is at most %LRG_PROFESSION_SKILL_LIMIT.
 *
 * Returns: %TRUE when the band is well ordered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skill_band_is_valid (const LrgSkillBand *self);

/**
 * lrg_skill_band_get_difficulty:
 * @self: an #LrgSkillBand
 * @skill: current skill
 *
 * Classifies @skill against the band: below orange is
 * %LRG_SKILL_DIFFICULTY_UNAVAILABLE, below yellow ORANGE, below green
 * YELLOW, below grey GREEN, otherwise GREY. An invalid band always yields
 * %LRG_SKILL_DIFFICULTY_UNAVAILABLE.
 *
 * Returns: the difficulty colour
 */
LRG_AVAILABLE_IN_ALL
LrgSkillDifficulty lrg_skill_band_get_difficulty (const LrgSkillBand *self,
                                                  guint               skill);

/**
 * lrg_skill_band_get_chance:
 * @self: an #LrgSkillBand
 * @skill: current skill
 *
 * Probability of gaining a skill point, using the classic WoW formula:
 * UNAVAILABLE and GREY give 0, ORANGE gives 1, YELLOW and GREEN give
 * `(grey - skill) / (grey - yellow)` clamped to [0, 1]. An invalid band
 * gives 0.
 *
 * Returns: chance in [0, 1]
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_skill_band_get_chance (const LrgSkillBand *self,
                                   guint               skill);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSkillBand, lrg_skill_band_free)

G_END_DECLS
