/* lrg-bt-judgment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The judgment conditions: LrgBTCondition nodes whose function reads the
 * blackboard key written from a decision model's answers.
 */

#include "lrg-bt-judgment.h"
#include "lrg-decision.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* what a judgment condition looks for */
typedef struct
{
    gchar           *key;
    LrgJudgmentBand  band;
    gdouble          yes;
    gdouble          no;
    gchar           *option;    /* a choice's: the option wanted */
} Want;

static void
want_free (gpointer data)
{
    Want *want = data;

    g_free (want->key);
    g_free (want->option);
    g_free (want);
}

/* the Noul at the key, in its band */
static gboolean
judgment_holds (LrgBlackboard *blackboard,
                gpointer       user_data)
{
    Want *want = user_data;
    LrgJudgmentBand band;
    gdouble p;

    if (!lrg_blackboard_has_key (blackboard, want->key))
        band = LRG_JUDGMENT_BAND_MISSING;
    else
    {
        p = lrg_blackboard_get_float (blackboard, want->key, -1.0f);
        if (p < 0.0)
            band = LRG_JUDGMENT_BAND_MISSING;
        else if (p >= want->yes)
            band = LRG_JUDGMENT_BAND_YES;
        else if (p <= want->no)
            band = LRG_JUDGMENT_BAND_NO;
        else
            band = LRG_JUDGMENT_BAND_UNSETTLED;
    }
    return band == want->band;
}

/* the Choice at the key, the option wanted */
static gboolean
choice_holds (LrgBlackboard *blackboard,
              gpointer       user_data)
{
    Want *want = user_data;

    return g_strcmp0 (lrg_blackboard_get_string (blackboard, want->key), want->option) == 0;
}

LrgBTCondition *
lrg_bt_condition_new_judgment (const gchar     *key,
                               LrgJudgmentBand  band,
                               gdouble          yes,
                               gdouble          no)
{
    Want *want;

    g_return_val_if_fail (key != NULL, NULL);
    g_return_val_if_fail (no <= yes, NULL);

    want = g_new0 (Want, 1);
    want->key = g_strdup (key);
    want->band = band;
    want->yes = yes;
    want->no = no;
    return lrg_bt_condition_new (judgment_holds, want, want_free);
}

LrgBTCondition *
lrg_bt_condition_new_choice (const gchar *key,
                             const gchar *option)
{
    Want *want;

    g_return_val_if_fail (key != NULL, NULL);
    g_return_val_if_fail (option != NULL, NULL);

    want = g_new0 (Want, 1);
    want->key = g_strdup (key);
    want->option = g_strdup (option);
    return lrg_bt_condition_new (choice_holds, want, want_free);
}
