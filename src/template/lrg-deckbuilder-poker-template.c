/* lrg-deckbuilder-poker-template.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of LrgDeckbuilderPokerTemplate - a Balatro-style
 * poker deckbuilder template that extends LrgDeckbuilderTemplate
 * with hand evaluation, scoring, and joker management.
 */

#include "config.h"
#include "lrg-deckbuilder-poker-template.h"
#include "../deckbuilder/lrg-scoring-context.h"
#include "../deckbuilder/lrg-scoring-hand.h"
#include "../deckbuilder/lrg-scoring-rules.h"
#include "../deckbuilder/lrg-joker-instance.h"
#include "../deckbuilder/lrg-joker-def.h"
#include "../deckbuilder/lrg-deck-instance.h"
#include "../deckbuilder/lrg-hand.h"
#include "../deckbuilder/lrg-card-pile.h"
#include "../deckbuilder/lrg-card-instance.h"
#include "../lrg-log.h"

/* Default values */
#define DEFAULT_MAX_HANDS      4
#define DEFAULT_MAX_DISCARDS   3
#define DEFAULT_MAX_JOKERS     5
#define DEFAULT_HAND_SIZE      8
#define DEFAULT_BLIND_SCORE    300

struct _LrgDeckbuilderPokerTemplate
{
    LrgDeckbuilderTemplate  parent_instance;

    LrgScoringContext      *scoring_context;
    LrgScoringHand         *scoring_hand;
    LrgScoringRules        *scoring_rules;
    GPtrArray              *jokers;

    gint64                  score;
    gint64                  blind_score;
    gint64                  money;
    guint                   ante;

    guint                   hands_remaining;
    guint                   discards_remaining;
    guint                   max_hands;
    guint                   max_discards;
    guint                   max_jokers;

    LrgHandType             last_hand_type;
    gint64                  last_hand_score;

    gboolean                in_round;
};

/* Property IDs */
enum
{
    PROP_0,
    PROP_SCORE,
    PROP_BLIND_SCORE,
    PROP_MONEY,
    PROP_ANTE,
    PROP_HANDS_REMAINING,
    PROP_DISCARDS_REMAINING,
    PROP_MAX_HANDS,
    PROP_MAX_DISCARDS,
    PROP_MAX_JOKERS,
    PROP_IN_ROUND,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signals */
enum
{
    SIGNAL_ROUND_STARTED,
    SIGNAL_ROUND_ENDED,
    SIGNAL_HAND_PLAYED,
    SIGNAL_CARDS_DISCARDED,
    SIGNAL_JOKER_ADDED,
    SIGNAL_JOKER_REMOVED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgDeckbuilderPokerTemplate, lrg_deckbuilder_poker_template,
                     LRG_TYPE_DECKBUILDER_TEMPLATE)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
apply_joker_scoring (LrgDeckbuilderPokerTemplate *self)
{
    guint i;

    if (self->jokers == NULL)
        return;

    /* Set jokers in scoring context for triggering */
    lrg_scoring_context_set_jokers (self->scoring_context, self->jokers);

    /* Iterate through jokers, check if each can trigger in the current
     * scoring context, and apply its effect if so.
     */
    for (i = 0; i < self->jokers->len; i++)
    {
        LrgJokerInstance *joker = g_ptr_array_index (self->jokers, i);
        LrgJokerDef *def = lrg_joker_instance_get_def (joker);

        if (lrg_joker_def_can_trigger (def, self->scoring_context, joker))
        {
            lrg_joker_def_apply_effect (def, self->scoring_context, joker);
        }
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_deckbuilder_poker_template_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
    LrgDeckbuilderPokerTemplate *self = LRG_DECKBUILDER_POKER_TEMPLATE (object);

    switch (prop_id)
    {
    case PROP_BLIND_SCORE:
        self->blind_score = g_value_get_int64 (value);
        break;

    case PROP_MONEY:
        self->money = g_value_get_int64 (value);
        break;

    case PROP_ANTE:
        self->ante = g_value_get_uint (value);
        break;

    case PROP_MAX_HANDS:
        self->max_hands = g_value_get_uint (value);
        break;

    case PROP_MAX_DISCARDS:
        self->max_discards = g_value_get_uint (value);
        break;

    case PROP_MAX_JOKERS:
        self->max_jokers = g_value_get_uint (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_poker_template_get_property (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
    LrgDeckbuilderPokerTemplate *self = LRG_DECKBUILDER_POKER_TEMPLATE (object);

    switch (prop_id)
    {
    case PROP_SCORE:
        g_value_set_int64 (value, self->score);
        break;

    case PROP_BLIND_SCORE:
        g_value_set_int64 (value, self->blind_score);
        break;

    case PROP_MONEY:
        g_value_set_int64 (value, self->money);
        break;

    case PROP_ANTE:
        g_value_set_uint (value, self->ante);
        break;

    case PROP_HANDS_REMAINING:
        g_value_set_uint (value, self->hands_remaining);
        break;

    case PROP_DISCARDS_REMAINING:
        g_value_set_uint (value, self->discards_remaining);
        break;

    case PROP_MAX_HANDS:
        g_value_set_uint (value, self->max_hands);
        break;

    case PROP_MAX_DISCARDS:
        g_value_set_uint (value, self->max_discards);
        break;

    case PROP_MAX_JOKERS:
        g_value_set_uint (value, self->max_jokers);
        break;

    case PROP_IN_ROUND:
        g_value_set_boolean (value, self->in_round);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_poker_template_dispose (GObject *object)
{
    LrgDeckbuilderPokerTemplate *self = LRG_DECKBUILDER_POKER_TEMPLATE (object);

    g_clear_object (&self->scoring_context);
    g_clear_object (&self->scoring_hand);
    g_clear_object (&self->scoring_rules);
    g_clear_pointer (&self->jokers, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_deckbuilder_poker_template_parent_class)->dispose (object);
}

static void
lrg_deckbuilder_poker_template_init (LrgDeckbuilderPokerTemplate *self)
{
    self->scoring_context = lrg_scoring_context_new ();
    self->scoring_hand = lrg_scoring_hand_new ();
    self->scoring_rules = NULL;
    self->jokers = g_ptr_array_new_with_free_func (g_object_unref);

    self->score = 0;
    self->blind_score = DEFAULT_BLIND_SCORE;
    self->money = 0;
    self->ante = 1;

    self->hands_remaining = DEFAULT_MAX_HANDS;
    self->discards_remaining = DEFAULT_MAX_DISCARDS;
    self->max_hands = DEFAULT_MAX_HANDS;
    self->max_discards = DEFAULT_MAX_DISCARDS;
    self->max_jokers = DEFAULT_MAX_JOKERS;

    self->last_hand_type = LRG_HAND_TYPE_NONE;
    self->last_hand_score = 0;

    self->in_round = FALSE;

    /* Set larger hand size for poker games */
    lrg_deckbuilder_template_set_base_hand_size (LRG_DECKBUILDER_TEMPLATE (self),
                                                  DEFAULT_HAND_SIZE);
}

static void
lrg_deckbuilder_poker_template_class_init (LrgDeckbuilderPokerTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_deckbuilder_poker_template_set_property;
    object_class->get_property = lrg_deckbuilder_poker_template_get_property;
    object_class->dispose = lrg_deckbuilder_poker_template_dispose;

    /* Properties */
    properties[PROP_SCORE] =
        g_param_spec_int64 ("score",
                            "Score",
                            "Current score this round",
                            0, G_MAXINT64, 0,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BLIND_SCORE] =
        g_param_spec_int64 ("blind-score",
                            "Blind Score",
                            "Score needed to beat the blind",
                            0, G_MAXINT64, DEFAULT_BLIND_SCORE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MONEY] =
        g_param_spec_int64 ("money",
                            "Money",
                            "Current money",
                            0, G_MAXINT64, 0,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ANTE] =
        g_param_spec_uint ("ante",
                           "Ante",
                           "Current ante level",
                           1, 100, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HANDS_REMAINING] =
        g_param_spec_uint ("hands-remaining",
                           "Hands Remaining",
                           "Number of hands remaining",
                           0, 100, DEFAULT_MAX_HANDS,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DISCARDS_REMAINING] =
        g_param_spec_uint ("discards-remaining",
                           "Discards Remaining",
                           "Number of discards remaining",
                           0, 100, DEFAULT_MAX_DISCARDS,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HANDS] =
        g_param_spec_uint ("max-hands",
                           "Max Hands",
                           "Maximum hands per round",
                           1, 100, DEFAULT_MAX_HANDS,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_DISCARDS] =
        g_param_spec_uint ("max-discards",
                           "Max Discards",
                           "Maximum discards per round",
                           0, 100, DEFAULT_MAX_DISCARDS,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_JOKERS] =
        g_param_spec_uint ("max-jokers",
                           "Max Jokers",
                           "Maximum jokers allowed",
                           0, 50, DEFAULT_MAX_JOKERS,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_IN_ROUND] =
        g_param_spec_boolean ("in-round",
                              "In Round",
                              "Whether currently in a round",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    signals[SIGNAL_ROUND_STARTED] =
        g_signal_new ("round-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_ROUND_ENDED] =
        g_signal_new ("round-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[SIGNAL_HAND_PLAYED] =
        g_signal_new ("hand-played",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT64);

    signals[SIGNAL_CARDS_DISCARDED] =
        g_signal_new ("cards-discarded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_JOKER_ADDED] =
        g_signal_new ("joker-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    signals[SIGNAL_JOKER_REMOVED] =
        g_signal_new ("joker-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_new:
 *
 * Creates a new poker template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderPokerTemplate
 */
LrgDeckbuilderPokerTemplate *
lrg_deckbuilder_poker_template_new (void)
{
    return g_object_new (LRG_TYPE_DECKBUILDER_POKER_TEMPLATE, NULL);
}

/* ==========================================================================
 * Public API - Scoring Context
 * ========================================================================== */

LrgScoringContext *
lrg_deckbuilder_poker_template_get_scoring_context (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), NULL);
    return self->scoring_context;
}

LrgScoringHand *
lrg_deckbuilder_poker_template_get_scoring_hand (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), NULL);
    return self->scoring_hand;
}

LrgScoringRules *
lrg_deckbuilder_poker_template_get_scoring_rules (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), NULL);
    return self->scoring_rules;
}

void
lrg_deckbuilder_poker_template_set_scoring_rules (LrgDeckbuilderPokerTemplate *self,
                                                   LrgScoringRules             *rules)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));

    if (self->scoring_rules != rules)
    {
        g_clear_object (&self->scoring_rules);
        if (rules != NULL)
            self->scoring_rules = g_object_ref (rules);
    }
}

/* ==========================================================================
 * Public API - Score & Progress
 * ========================================================================== */

gint64
lrg_deckbuilder_poker_template_get_score (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->score;
}

gint64
lrg_deckbuilder_poker_template_get_blind_score (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->blind_score;
}

void
lrg_deckbuilder_poker_template_set_blind_score (LrgDeckbuilderPokerTemplate *self,
                                                 gint64                       score)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->blind_score = score;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLIND_SCORE]);
}

guint
lrg_deckbuilder_poker_template_get_ante (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->ante;
}

void
lrg_deckbuilder_poker_template_set_ante (LrgDeckbuilderPokerTemplate *self,
                                          guint                        ante)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->ante = ante;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANTE]);
}

gint64
lrg_deckbuilder_poker_template_get_money (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->money;
}

void
lrg_deckbuilder_poker_template_set_money (LrgDeckbuilderPokerTemplate *self,
                                           gint64                       money)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->money = money;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MONEY]);
}

void
lrg_deckbuilder_poker_template_add_money (LrgDeckbuilderPokerTemplate *self,
                                           gint64                       amount)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->money += amount;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MONEY]);
}

/* ==========================================================================
 * Public API - Hands & Discards
 * ========================================================================== */

guint
lrg_deckbuilder_poker_template_get_hands_remaining (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->hands_remaining;
}

void
lrg_deckbuilder_poker_template_set_hands_remaining (LrgDeckbuilderPokerTemplate *self,
                                                     guint                        hands)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->hands_remaining = hands;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDS_REMAINING]);
}

guint
lrg_deckbuilder_poker_template_get_discards_remaining (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->discards_remaining;
}

void
lrg_deckbuilder_poker_template_set_discards_remaining (LrgDeckbuilderPokerTemplate *self,
                                                        guint                        discards)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->discards_remaining = discards;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISCARDS_REMAINING]);
}

guint
lrg_deckbuilder_poker_template_get_max_hands (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->max_hands;
}

void
lrg_deckbuilder_poker_template_set_max_hands (LrgDeckbuilderPokerTemplate *self,
                                               guint                        max_hands)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->max_hands = max_hands;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HANDS]);
}

guint
lrg_deckbuilder_poker_template_get_max_discards (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->max_discards;
}

void
lrg_deckbuilder_poker_template_set_max_discards (LrgDeckbuilderPokerTemplate *self,
                                                  guint                        max_discards)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->max_discards = max_discards;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DISCARDS]);
}

/* ==========================================================================
 * Public API - Joker Management
 * ========================================================================== */

void
lrg_deckbuilder_poker_template_add_joker (LrgDeckbuilderPokerTemplate *self,
                                           LrgJokerInstance            *joker)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (joker));

    if (self->jokers->len >= self->max_jokers)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Cannot add joker - at max capacity");
        g_object_unref (joker);
        return;
    }

    g_ptr_array_add (self->jokers, joker);
    g_signal_emit (self, signals[SIGNAL_JOKER_ADDED], 0, joker);
}

LrgJokerInstance *
lrg_deckbuilder_poker_template_add_joker_from_def (LrgDeckbuilderPokerTemplate *self,
                                                    LrgJokerDef                 *def)
{
    LrgJokerInstance *joker;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), NULL);
    g_return_val_if_fail (LRG_IS_JOKER_DEF (def), NULL);

    if (self->jokers->len >= self->max_jokers)
        return NULL;

    joker = lrg_joker_instance_new (def);
    lrg_deckbuilder_poker_template_add_joker (self, joker);

    return joker;
}

void
lrg_deckbuilder_poker_template_remove_joker (LrgDeckbuilderPokerTemplate *self,
                                              LrgJokerInstance            *joker)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (joker));

    if (g_ptr_array_remove (self->jokers, joker))
        g_signal_emit (self, signals[SIGNAL_JOKER_REMOVED], 0, joker);
}

GPtrArray *
lrg_deckbuilder_poker_template_get_jokers (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), NULL);
    return self->jokers;
}

guint
lrg_deckbuilder_poker_template_get_joker_count (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->jokers->len;
}

guint
lrg_deckbuilder_poker_template_get_max_jokers (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->max_jokers;
}

void
lrg_deckbuilder_poker_template_set_max_jokers (LrgDeckbuilderPokerTemplate *self,
                                                guint                        max_jokers)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));
    self->max_jokers = max_jokers;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_JOKERS]);
}

/* ==========================================================================
 * Public API - Round Management
 * ========================================================================== */

void
lrg_deckbuilder_poker_template_start_round (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    guint hand_size;

    g_return_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self));

    /* Set up deck */
    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck != NULL)
        lrg_deck_instance_setup (deck);

    /* Reset round state */
    self->score = 0;
    self->hands_remaining = self->max_hands;
    self->discards_remaining = self->max_discards;
    self->last_hand_type = LRG_HAND_TYPE_NONE;
    self->last_hand_score = 0;
    self->in_round = TRUE;

    /* Draw initial hand */
    hand_size = lrg_deckbuilder_template_get_base_hand_size (LRG_DECKBUILDER_TEMPLATE (self));
    lrg_deck_mixin_draw_cards (LRG_DECK_MIXIN (self), hand_size);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCORE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDS_REMAINING]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISCARDS_REMAINING]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_ROUND]);

    g_signal_emit (self, signals[SIGNAL_ROUND_STARTED], 0);

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Poker round started (blind: %ld)",
                  self->blind_score);
}

gboolean
lrg_deckbuilder_poker_template_end_round (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    gboolean won;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);

    won = self->score >= self->blind_score;
    self->in_round = FALSE;

    /* Clean up deck */
    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck != NULL)
        lrg_deck_instance_end_combat (deck);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_ROUND]);
    g_signal_emit (self, signals[SIGNAL_ROUND_ENDED], 0, won);

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Poker round ended (score: %ld, won: %s)",
                  self->score, won ? "yes" : "no");

    return won;
}

gboolean
lrg_deckbuilder_poker_template_is_in_round (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);
    return self->in_round;
}

gboolean
lrg_deckbuilder_poker_template_is_round_won (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);
    return self->score >= self->blind_score;
}

gboolean
lrg_deckbuilder_poker_template_is_round_lost (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);
    return self->hands_remaining == 0 && self->score < self->blind_score;
}

/* ==========================================================================
 * Public API - Hand Operations
 * ========================================================================== */

gint64
lrg_deckbuilder_poker_template_play_hand (LrgDeckbuilderPokerTemplate *self,
                                           GPtrArray                   *cards)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    LrgCardPile *discard;
    LrgHandType hand_type;
    gint64 hand_score;
    gint64 base_chips = 0;
    gint64 base_mult = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    g_return_val_if_fail (cards != NULL, 0);

    if (!self->in_round || self->hands_remaining == 0)
        return 0;

    if (cards->len == 0 || cards->len > 5)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Invalid number of cards for hand");
        return 0;
    }

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return 0;

    /* Evaluate the hand */
    lrg_scoring_hand_set_cards (self->scoring_hand, cards);
    hand_type = lrg_scoring_hand_evaluate (self->scoring_hand);

    /* Get base values from scoring rules */
    if (self->scoring_rules != NULL)
    {
        base_chips = lrg_scoring_rules_get_base_chips (self->scoring_rules, hand_type);
        base_mult = lrg_scoring_rules_get_base_mult (self->scoring_rules, hand_type);
    }
    else
    {
        /* Default values if no rules set */
        switch (hand_type)
        {
        case LRG_HAND_TYPE_HIGH_CARD:    base_chips = 5;   base_mult = 1; break;
        case LRG_HAND_TYPE_PAIR:         base_chips = 10;  base_mult = 2; break;
        case LRG_HAND_TYPE_TWO_PAIR:     base_chips = 20;  base_mult = 2; break;
        case LRG_HAND_TYPE_THREE_OF_A_KIND: base_chips = 30;  base_mult = 3; break;
        case LRG_HAND_TYPE_STRAIGHT:     base_chips = 30;  base_mult = 4; break;
        case LRG_HAND_TYPE_FLUSH:        base_chips = 35;  base_mult = 4; break;
        case LRG_HAND_TYPE_FULL_HOUSE:   base_chips = 40;  base_mult = 4; break;
        case LRG_HAND_TYPE_FOUR_OF_A_KIND: base_chips = 60;  base_mult = 7; break;
        case LRG_HAND_TYPE_STRAIGHT_FLUSH: base_chips = 100; base_mult = 8; break;
        case LRG_HAND_TYPE_ROYAL_FLUSH:  base_chips = 100; base_mult = 8; break;
        default:                          base_chips = 0;   base_mult = 1; break;
        }
    }

    /* Set up scoring context */
    lrg_scoring_context_reset (self->scoring_context);
    lrg_scoring_context_set_hand_type (self->scoring_context, hand_type);
    lrg_scoring_context_set_scoring_cards (self->scoring_context,
                                            lrg_scoring_hand_get_scoring_cards (self->scoring_hand));
    lrg_scoring_context_set_base_chips (self->scoring_context, base_chips);
    lrg_scoring_context_set_base_mult (self->scoring_context, base_mult);

    /* Add chip values from scoring cards */
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        gint chip_value = lrg_card_instance_get_total_chip_value (card);
        lrg_scoring_context_add_chips (self->scoring_context, chip_value);
    }

    /* Apply joker effects */
    apply_joker_scoring (self);

    /* Calculate final score */
    hand_score = lrg_scoring_context_calculate_score (self->scoring_context);

    /* Update state */
    self->score += hand_score;
    self->hands_remaining--;
    self->last_hand_type = hand_type;
    self->last_hand_score = hand_score;

    /* Move played cards to discard */
    hand = lrg_deck_instance_get_hand (deck);
    discard = lrg_deck_instance_get_discard_pile (deck);
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        lrg_hand_discard (hand, card, discard);
    }

    /* Draw back up to hand size */
    {
        guint hand_count = lrg_hand_get_count (hand);
        guint target_size = lrg_deckbuilder_template_get_base_hand_size (LRG_DECKBUILDER_TEMPLATE (self));
        if (hand_count < target_size)
            lrg_deck_mixin_draw_cards (LRG_DECK_MIXIN (self), target_size - hand_count);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCORE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDS_REMAINING]);

    g_signal_emit (self, signals[SIGNAL_HAND_PLAYED], 0, hand_type, hand_score);

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Played hand type %d for %" G_GINT64_FORMAT " (total: %" G_GINT64_FORMAT ")",
              hand_type, hand_score, self->score);

    return hand_score;
}

gint64
lrg_deckbuilder_poker_template_play_selected (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    GPtrArray *selected;
    gint64 result;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return 0;

    hand = lrg_deck_instance_get_hand (deck);
    selected = lrg_hand_get_selected (hand);

    if (selected == NULL || selected->len == 0)
        return 0;

    result = lrg_deckbuilder_poker_template_play_hand (self, selected);
    lrg_hand_clear_selection (hand);

    return result;
}

gboolean
lrg_deckbuilder_poker_template_discard_cards (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    LrgCardPile *discard;
    guint cards_discarded;
    guint hand_count;
    guint target_size;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);
    g_return_val_if_fail (cards != NULL, FALSE);

    if (!self->in_round || self->discards_remaining == 0)
        return FALSE;

    if (cards->len == 0)
        return FALSE;

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return FALSE;

    hand = lrg_deck_instance_get_hand (deck);
    discard = lrg_deck_instance_get_discard_pile (deck);

    /* Discard selected cards */
    cards_discarded = 0;
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        if (lrg_hand_discard (hand, card, discard))
            cards_discarded++;
    }

    if (cards_discarded == 0)
        return FALSE;

    self->discards_remaining--;

    /* Draw replacements */
    hand_count = lrg_hand_get_count (hand);
    target_size = lrg_deckbuilder_template_get_base_hand_size (LRG_DECKBUILDER_TEMPLATE (self));
    if (hand_count < target_size)
        lrg_deck_mixin_draw_cards (LRG_DECK_MIXIN (self), target_size - hand_count);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISCARDS_REMAINING]);
    g_signal_emit (self, signals[SIGNAL_CARDS_DISCARDED], 0, cards_discarded);

    return TRUE;
}

gboolean
lrg_deckbuilder_poker_template_discard_selected (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    GPtrArray *selected;
    gboolean result;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return FALSE;

    hand = lrg_deck_instance_get_hand (deck);
    selected = lrg_hand_get_selected (hand);

    if (selected == NULL || selected->len == 0)
        return FALSE;

    result = lrg_deckbuilder_poker_template_discard_cards (self, selected);
    lrg_hand_clear_selection (hand);

    return result;
}

gboolean
lrg_deckbuilder_poker_template_can_play_hand (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    GPtrArray *selected;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);

    if (!self->in_round || self->hands_remaining == 0)
        return FALSE;

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return FALSE;

    hand = lrg_deck_instance_get_hand (deck);
    selected = lrg_hand_get_selected (hand);

    return selected != NULL && selected->len >= 1 && selected->len <= 5;
}

gboolean
lrg_deckbuilder_poker_template_can_discard (LrgDeckbuilderPokerTemplate *self)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    GPtrArray *selected;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), FALSE);

    if (!self->in_round || self->discards_remaining == 0)
        return FALSE;

    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck == NULL)
        return FALSE;

    hand = lrg_deck_instance_get_hand (deck);
    selected = lrg_hand_get_selected (hand);

    return selected != NULL && selected->len > 0;
}

LrgHandType
lrg_deckbuilder_poker_template_evaluate_hand (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), LRG_HAND_TYPE_NONE);
    g_return_val_if_fail (cards != NULL, LRG_HAND_TYPE_NONE);

    lrg_scoring_hand_set_cards (self->scoring_hand, cards);
    return lrg_scoring_hand_evaluate (self->scoring_hand);
}

gint64
lrg_deckbuilder_poker_template_preview_score (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards)
{
    LrgHandType hand_type;
    gint64 base_chips = 0;
    gint64 base_mult = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    g_return_val_if_fail (cards != NULL, 0);

    if (cards->len == 0)
        return 0;

    /* Evaluate hand type */
    lrg_scoring_hand_set_cards (self->scoring_hand, cards);
    hand_type = lrg_scoring_hand_evaluate (self->scoring_hand);

    /* Get base values */
    if (self->scoring_rules != NULL)
    {
        base_chips = lrg_scoring_rules_get_base_chips (self->scoring_rules, hand_type);
        base_mult = lrg_scoring_rules_get_base_mult (self->scoring_rules, hand_type);
    }
    else
    {
        switch (hand_type)
        {
        case LRG_HAND_TYPE_HIGH_CARD:    base_chips = 5;   base_mult = 1; break;
        case LRG_HAND_TYPE_PAIR:         base_chips = 10;  base_mult = 2; break;
        case LRG_HAND_TYPE_TWO_PAIR:     base_chips = 20;  base_mult = 2; break;
        case LRG_HAND_TYPE_THREE_OF_A_KIND: base_chips = 30;  base_mult = 3; break;
        case LRG_HAND_TYPE_STRAIGHT:     base_chips = 30;  base_mult = 4; break;
        case LRG_HAND_TYPE_FLUSH:        base_chips = 35;  base_mult = 4; break;
        case LRG_HAND_TYPE_FULL_HOUSE:   base_chips = 40;  base_mult = 4; break;
        case LRG_HAND_TYPE_FOUR_OF_A_KIND: base_chips = 60;  base_mult = 7; break;
        case LRG_HAND_TYPE_STRAIGHT_FLUSH: base_chips = 100; base_mult = 8; break;
        case LRG_HAND_TYPE_ROYAL_FLUSH:  base_chips = 100; base_mult = 8; break;
        default:                          return 0;
        }
    }

    /* Add card chip values */
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        base_chips += lrg_card_instance_get_total_chip_value (card);
    }

    return base_chips * base_mult;
}

LrgHandType
lrg_deckbuilder_poker_template_get_last_hand_type (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), LRG_HAND_TYPE_NONE);
    return self->last_hand_type;
}

gint64
lrg_deckbuilder_poker_template_get_last_hand_score (LrgDeckbuilderPokerTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_POKER_TEMPLATE (self), 0);
    return self->last_hand_score;
}
