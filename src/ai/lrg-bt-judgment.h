/* lrg-bt-judgment.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Behavior tree conditions over a decision model's judgments.
 *
 * LrgDecisionBrain (or lrg_judgments_to_blackboard()) writes a model's
 * answers to a blackboard; these conditions let a behavior tree branch
 * on them, so the decision itself stays in the tree, in code: "the
 * target is a quest's (yes) and the health is high" picks the pull.
 */

#ifndef LRG_BT_JUDGMENT_H
#define LRG_BT_JUDGMENT_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"
#include "lrg-bt-leaf.h"

G_BEGIN_DECLS

/**
 * lrg_bt_condition_new_judgment:
 * @key: a Noul's key on the blackboard ("combat.quest_target")
 * @band: the band that succeeds
 * @yes: the probability at or above which it is yes (%LRG_JUDGMENT_YES)
 * @no: the probability at or below which it is no (%LRG_JUDGMENT_NO)
 *
 * A condition that succeeds when the Noul at @key reads as @band (a key
 * not there is %LRG_JUDGMENT_BAND_MISSING).
 *
 * Returns: (transfer full): the condition
 */
LRG_AVAILABLE_IN_ALL
LrgBTCondition *
lrg_bt_condition_new_judgment (const gchar     *key,
                               LrgJudgmentBand  band,
                               gdouble          yes,
                               gdouble          no);

/**
 * lrg_bt_condition_new_choice:
 * @key: a Choice's key on the blackboard
 * @option: the option that succeeds
 *
 * A condition that succeeds when the Choice at @key picked @option.
 *
 * Returns: (transfer full): the condition
 */
LRG_AVAILABLE_IN_ALL
LrgBTCondition *
lrg_bt_condition_new_choice (const gchar *key,
                             const gchar *option);

G_END_DECLS

#endif /* LRG_BT_JUDGMENT_H */
