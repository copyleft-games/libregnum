/* lrg-deck-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-deck-instance.h"
#include "lrg-deck-def.h"
#include "lrg-card-def.h"
#include "lrg-card-instance.h"
#include "lrg-card-pile.h"
#include "lrg-hand.h"
#include "../save/lrg-saveable.h"
#include "../save/lrg-save-context.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgDeckInstance:
 *
 * Runtime state of a deck during a run.
 *
 * Manages the draw pile, discard pile, exhaust pile, hand, and
 * master deck list. The master deck contains all cards currently
 * in the run (across all piles).
 */

static void lrg_deck_instance_saveable_init (LrgSaveableInterface *iface);

struct _LrgDeckInstance
{
    GObject parent_instance;

    LrgDeckDef  *deck_def;
    guint32      seed;
    GRand       *rng;

    LrgCardPile *draw_pile;
    LrgCardPile *discard_pile;
    LrgCardPile *exhaust_pile;
    LrgHand     *hand;

    GPtrArray   *master_deck;  /* All cards in the run */
    gboolean     is_setup;
};

G_DEFINE_TYPE_WITH_CODE (LrgDeckInstance, lrg_deck_instance, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                lrg_deck_instance_saveable_init))

enum
{
    PROP_0,
    PROP_DECK_DEF,
    PROP_SEED,
    PROP_DRAW_PILE,
    PROP_DISCARD_PILE,
    PROP_EXHAUST_PILE,
    PROP_HAND,
    PROP_TOTAL_CARDS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CARD_ADDED,
    SIGNAL_CARD_REMOVED,
    SIGNAL_DECK_SHUFFLED,
    SIGNAL_CARD_DRAWN,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * LrgSaveable implementation
 * ========================================================================== */

static const gchar *
lrg_deck_instance_get_save_id (LrgSaveable *saveable)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (saveable);

    if (self->deck_def != NULL)
        return lrg_deck_def_get_id (self->deck_def);

    return "deck-instance";
}

static gboolean
lrg_deck_instance_save (LrgSaveable     *saveable,
                        LrgSaveContext  *context,
                        GError         **error)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (saveable);
    guint i;

    /* Save seed for RNG reproducibility */
    lrg_save_context_write_uint (context, "seed", self->seed);

    /* Save deck def ID */
    if (self->deck_def != NULL)
    {
        lrg_save_context_write_string (context, "deck-def-id",
                                       lrg_deck_def_get_id (self->deck_def));
    }

    /* Save master deck count */
    lrg_save_context_write_uint (context, "card-count", self->master_deck->len);

    /*
     * Save each card as separate keys:
     * card-0-def-id, card-0-upgrade-tier, card-0-zone
     * card-1-def-id, card-1-upgrade-tier, card-1-zone
     * etc.
     */
    for (i = 0; i < self->master_deck->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->master_deck, i);
        LrgCardDef *def = lrg_card_instance_get_def (card);
        g_autofree gchar *key_def = NULL;
        g_autofree gchar *key_tier = NULL;
        g_autofree gchar *key_zone = NULL;

        key_def = g_strdup_printf ("card-%u-def-id", i);
        key_tier = g_strdup_printf ("card-%u-upgrade-tier", i);
        key_zone = g_strdup_printf ("card-%u-zone", i);

        lrg_save_context_write_string (context, key_def,
                                       lrg_card_def_get_id (def));
        lrg_save_context_write_uint (context, key_tier,
                                     lrg_card_instance_get_upgrade_tier (card));
        lrg_save_context_write_int (context, key_zone,
                                    (gint64) lrg_card_instance_get_zone (card));
    }

    return TRUE;
}

static gboolean
lrg_deck_instance_load (LrgSaveable     *saveable,
                        LrgSaveContext  *context,
                        GError         **error)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (saveable);

    /* Restore seed */
    self->seed = (guint32) lrg_save_context_read_uint (context, "seed", 0);
    g_rand_set_seed (self->rng, self->seed);

    /*
     * Note: Full loading requires a card registry to resolve
     * card def IDs to actual LrgCardDef objects. This would
     * typically be done by a higher-level loader that:
     *   1. Reads "card-count"
     *   2. For each i from 0 to card-count-1:
     *      - Read card-i-def-id string
     *      - Look up LrgCardDef from registry
     *      - Read card-i-upgrade-tier and card-i-zone
     *      - Create LrgCardInstance and place in correct zone
     */

    return TRUE;
}

static void
lrg_deck_instance_saveable_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = lrg_deck_instance_get_save_id;
    iface->save = lrg_deck_instance_save;
    iface->load = lrg_deck_instance_load;
}

/* ==========================================================================
 * GObject implementation
 * ========================================================================== */

static void
lrg_deck_instance_finalize (GObject *object)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (object);

    g_clear_object (&self->deck_def);
    g_clear_pointer (&self->rng, g_rand_free);
    g_clear_object (&self->draw_pile);
    g_clear_object (&self->discard_pile);
    g_clear_object (&self->exhaust_pile);
    g_clear_object (&self->hand);
    g_clear_pointer (&self->master_deck, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_deck_instance_parent_class)->finalize (object);
}

static void
lrg_deck_instance_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DECK_DEF:
        g_value_set_object (value, self->deck_def);
        break;
    case PROP_SEED:
        g_value_set_uint (value, self->seed);
        break;
    case PROP_DRAW_PILE:
        g_value_set_object (value, self->draw_pile);
        break;
    case PROP_DISCARD_PILE:
        g_value_set_object (value, self->discard_pile);
        break;
    case PROP_EXHAUST_PILE:
        g_value_set_object (value, self->exhaust_pile);
        break;
    case PROP_HAND:
        g_value_set_object (value, self->hand);
        break;
    case PROP_TOTAL_CARDS:
        g_value_set_uint (value, self->master_deck->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_instance_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgDeckInstance *self = LRG_DECK_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DECK_DEF:
        g_clear_object (&self->deck_def);
        self->deck_def = g_value_dup_object (value);
        break;
    case PROP_SEED:
        self->seed = g_value_get_uint (value);
        g_rand_set_seed (self->rng, self->seed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_instance_class_init (LrgDeckInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_deck_instance_finalize;
    object_class->get_property = lrg_deck_instance_get_property;
    object_class->set_property = lrg_deck_instance_set_property;

    /**
     * LrgDeckInstance:deck-def:
     *
     * The deck definition this instance is based on.
     *
     * Since: 1.0
     */
    properties[PROP_DECK_DEF] =
        g_param_spec_object ("deck-def",
                             "Deck Definition",
                             "The deck definition",
                             LRG_TYPE_DECK_DEF,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:seed:
     *
     * The random seed for shuffling.
     *
     * Since: 1.0
     */
    properties[PROP_SEED] =
        g_param_spec_uint ("seed",
                           "Seed",
                           "Random seed for shuffling",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:draw-pile:
     *
     * The draw pile.
     *
     * Since: 1.0
     */
    properties[PROP_DRAW_PILE] =
        g_param_spec_object ("draw-pile",
                             "Draw Pile",
                             "The draw pile",
                             LRG_TYPE_CARD_PILE,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:discard-pile:
     *
     * The discard pile.
     *
     * Since: 1.0
     */
    properties[PROP_DISCARD_PILE] =
        g_param_spec_object ("discard-pile",
                             "Discard Pile",
                             "The discard pile",
                             LRG_TYPE_CARD_PILE,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:exhaust-pile:
     *
     * The exhaust pile.
     *
     * Since: 1.0
     */
    properties[PROP_EXHAUST_PILE] =
        g_param_spec_object ("exhaust-pile",
                             "Exhaust Pile",
                             "The exhaust pile",
                             LRG_TYPE_CARD_PILE,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:hand:
     *
     * The hand.
     *
     * Since: 1.0
     */
    properties[PROP_HAND] =
        g_param_spec_object ("hand",
                             "Hand",
                             "The hand",
                             LRG_TYPE_HAND,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckInstance:total-cards:
     *
     * Total number of cards in the deck.
     *
     * Since: 1.0
     */
    properties[PROP_TOTAL_CARDS] =
        g_param_spec_uint ("total-cards",
                           "Total Cards",
                           "Total number of cards in deck",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgDeckInstance::card-added:
     * @self: the deck instance
     * @card: the added card
     *
     * Emitted when a card is added to the deck.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_ADDED] =
        g_signal_new ("card-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgDeckInstance::card-removed:
     * @self: the deck instance
     * @card: the removed card
     *
     * Emitted when a card is removed from the deck.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_REMOVED] =
        g_signal_new ("card-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgDeckInstance::deck-shuffled:
     * @self: the deck instance
     *
     * Emitted when the draw pile is shuffled.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DECK_SHUFFLED] =
        g_signal_new ("deck-shuffled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgDeckInstance::card-drawn:
     * @self: the deck instance
     * @card: the drawn card
     *
     * Emitted when a card is drawn.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_DRAWN] =
        g_signal_new ("card-drawn",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_INSTANCE);
}

static void
lrg_deck_instance_init (LrgDeckInstance *self)
{
    self->rng = g_rand_new ();
    self->draw_pile = lrg_card_pile_new_with_zone (LRG_ZONE_DRAW);
    self->discard_pile = lrg_card_pile_new_with_zone (LRG_ZONE_DISCARD);
    self->exhaust_pile = lrg_card_pile_new_with_zone (LRG_ZONE_EXHAUST);
    self->hand = lrg_hand_new ();
    self->master_deck = g_ptr_array_new_with_free_func (g_object_unref);
    self->is_setup = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_deck_instance_new:
 * @deck_def: the deck definition
 *
 * Creates a new deck instance from a definition.
 * Uses a random seed.
 *
 * Returns: (transfer full): a new #LrgDeckInstance
 *
 * Since: 1.0
 */
LrgDeckInstance *
lrg_deck_instance_new (LrgDeckDef *deck_def)
{
    g_return_val_if_fail (LRG_IS_DECK_DEF (deck_def), NULL);

    return g_object_new (LRG_TYPE_DECK_INSTANCE,
                         "deck-def", deck_def,
                         "seed", g_random_int (),
                         NULL);
}

/**
 * lrg_deck_instance_new_with_seed:
 * @deck_def: the deck definition
 * @seed: the random seed
 *
 * Creates a new deck instance with a specific seed.
 * Useful for deterministic runs.
 *
 * Returns: (transfer full): a new #LrgDeckInstance
 *
 * Since: 1.0
 */
LrgDeckInstance *
lrg_deck_instance_new_with_seed (LrgDeckDef *deck_def,
                                 guint32     seed)
{
    g_return_val_if_fail (LRG_IS_DECK_DEF (deck_def), NULL);

    return g_object_new (LRG_TYPE_DECK_INSTANCE,
                         "deck-def", deck_def,
                         "seed", seed,
                         NULL);
}

/**
 * lrg_deck_instance_get_def:
 * @self: a #LrgDeckInstance
 *
 * Gets the deck definition.
 *
 * Returns: (transfer none): the deck definition
 *
 * Since: 1.0
 */
LrgDeckDef *
lrg_deck_instance_get_def (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->deck_def;
}

/**
 * lrg_deck_instance_get_seed:
 * @self: a #LrgDeckInstance
 *
 * Gets the random seed.
 *
 * Returns: the seed
 *
 * Since: 1.0
 */
guint32
lrg_deck_instance_get_seed (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), 0);

    return self->seed;
}

/**
 * lrg_deck_instance_get_rng:
 * @self: a #LrgDeckInstance
 *
 * Gets the random number generator.
 *
 * Returns: (transfer none): the RNG
 *
 * Since: 1.0
 */
GRand *
lrg_deck_instance_get_rng (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->rng;
}

/**
 * lrg_deck_instance_get_draw_pile:
 * @self: a #LrgDeckInstance
 *
 * Gets the draw pile.
 *
 * Returns: (transfer none): the draw pile
 *
 * Since: 1.0
 */
LrgCardPile *
lrg_deck_instance_get_draw_pile (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->draw_pile;
}

/**
 * lrg_deck_instance_get_discard_pile:
 * @self: a #LrgDeckInstance
 *
 * Gets the discard pile.
 *
 * Returns: (transfer none): the discard pile
 *
 * Since: 1.0
 */
LrgCardPile *
lrg_deck_instance_get_discard_pile (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->discard_pile;
}

/**
 * lrg_deck_instance_get_exhaust_pile:
 * @self: a #LrgDeckInstance
 *
 * Gets the exhaust pile.
 *
 * Returns: (transfer none): the exhaust pile
 *
 * Since: 1.0
 */
LrgCardPile *
lrg_deck_instance_get_exhaust_pile (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->exhaust_pile;
}

/**
 * lrg_deck_instance_get_hand:
 * @self: a #LrgDeckInstance
 *
 * Gets the hand.
 *
 * Returns: (transfer none): the hand
 *
 * Since: 1.0
 */
LrgHand *
lrg_deck_instance_get_hand (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->hand;
}

/**
 * lrg_deck_instance_setup:
 * @self: a #LrgDeckInstance
 *
 * Sets up the deck from the definition's starting cards.
 * Creates card instances and adds them to the draw pile.
 *
 * Since: 1.0
 */
void
lrg_deck_instance_setup (LrgDeckInstance *self)
{
    GPtrArray *starting_cards;
    guint i;
    guint j;

    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));

    if (self->is_setup)
        return;

    if (self->deck_def == NULL)
        return;

    starting_cards = lrg_deck_def_get_starting_cards (self->deck_def);
    if (starting_cards == NULL)
        return;

    /* Create instances for each starting card */
    for (i = 0; i < starting_cards->len; i++)
    {
        LrgDeckCardEntry *entry = g_ptr_array_index (starting_cards, i);

        for (j = 0; j < entry->count; j++)
        {
            LrgCardInstance *instance;

            instance = lrg_card_instance_new (entry->card_def);
            g_ptr_array_add (self->master_deck, g_object_ref (instance));
            lrg_card_pile_add_top (self->draw_pile, instance);
        }
    }

    self->is_setup = TRUE;

    lrg_log_debug ("Set up deck with %u cards", self->master_deck->len);
}

/**
 * lrg_deck_instance_shuffle_draw_pile:
 * @self: a #LrgDeckInstance
 *
 * Shuffles the draw pile.
 *
 * Since: 1.0
 */
void
lrg_deck_instance_shuffle_draw_pile (LrgDeckInstance *self)
{
    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));

    lrg_card_pile_shuffle (self->draw_pile, self->rng);
    g_signal_emit (self, signals[SIGNAL_DECK_SHUFFLED], 0);

    lrg_log_debug ("Shuffled draw pile");
}

/**
 * lrg_deck_instance_shuffle_discard_into_draw:
 * @self: a #LrgDeckInstance
 *
 * Transfers all cards from discard to draw and shuffles.
 *
 * Since: 1.0
 */
void
lrg_deck_instance_shuffle_discard_into_draw (LrgDeckInstance *self)
{
    guint count;

    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));

    count = lrg_card_pile_transfer_all (self->discard_pile, self->draw_pile);
    if (count > 0)
    {
        lrg_card_pile_shuffle (self->draw_pile, self->rng);
        g_signal_emit (self, signals[SIGNAL_DECK_SHUFFLED], 0);
    }

    lrg_log_debug ("Shuffled %u cards from discard into draw", count);
}

/**
 * lrg_deck_instance_draw_card:
 * @self: a #LrgDeckInstance
 *
 * Draws a card from the draw pile to the hand.
 * If draw pile is empty, shuffles discard into draw first.
 *
 * Returns: (transfer none) (nullable): the drawn card, or %NULL if no cards
 *
 * Since: 1.0
 */
LrgCardInstance *
lrg_deck_instance_draw_card (LrgDeckInstance *self)
{
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    /* If draw pile empty, shuffle discard into draw */
    if (lrg_card_pile_is_empty (self->draw_pile))
    {
        if (lrg_card_pile_is_empty (self->discard_pile))
            return NULL;

        lrg_deck_instance_shuffle_discard_into_draw (self);
    }

    /* Draw from pile */
    card = lrg_card_pile_draw (self->draw_pile);
    if (card == NULL)
        return NULL;

    /* Add to hand */
    if (!lrg_hand_add (self->hand, card))
    {
        /* Hand full - discard */
        lrg_card_pile_add_top (self->discard_pile, card);
        g_object_unref (card);
        return NULL;
    }

    g_signal_emit (self, signals[SIGNAL_CARD_DRAWN], 0, card);

    /* Don't unref - hand now owns the reference */
    return card;
}

/**
 * lrg_deck_instance_draw_cards:
 * @self: a #LrgDeckInstance
 * @count: number of cards to draw
 *
 * Draws multiple cards.
 *
 * Returns: number of cards actually drawn
 *
 * Since: 1.0
 */
guint
lrg_deck_instance_draw_cards (LrgDeckInstance *self,
                              guint            count)
{
    guint drawn;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), 0);

    drawn = 0;
    for (i = 0; i < count; i++)
    {
        if (lrg_deck_instance_draw_card (self) != NULL)
            drawn++;
        else
            break;
    }

    return drawn;
}

/**
 * lrg_deck_instance_discard_hand:
 * @self: a #LrgDeckInstance
 *
 * Discards all cards in hand (respecting Retain keyword).
 *
 * Since: 1.0
 */
void
lrg_deck_instance_discard_hand (LrgDeckInstance *self)
{
    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));

    lrg_hand_discard_all (self->hand, self->discard_pile);
}

/**
 * lrg_deck_instance_end_combat:
 * @self: a #LrgDeckInstance
 *
 * Ends combat - moves all cards back to draw pile.
 * Cards in exhaust stay there (permanently removed for combat).
 *
 * Since: 1.0
 */
void
lrg_deck_instance_end_combat (LrgDeckInstance *self)
{
    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));

    /* Discard hand */
    lrg_hand_discard_all (self->hand, self->discard_pile);

    /* Transfer discard to draw */
    lrg_card_pile_transfer_all (self->discard_pile, self->draw_pile);

    lrg_log_debug ("Combat ended, deck reset");
}

/**
 * lrg_deck_instance_add_card:
 * @self: a #LrgDeckInstance
 * @card_def: the card definition
 *
 * Adds a new card to the deck (to discard pile).
 *
 * Since: 1.0
 */
void
lrg_deck_instance_add_card (LrgDeckInstance *self,
                            LrgCardDef      *card_def)
{
    LrgCardInstance *instance;

    g_return_if_fail (LRG_IS_DECK_INSTANCE (self));
    g_return_if_fail (LRG_IS_CARD_DEF (card_def));

    instance = lrg_card_instance_new (card_def);
    g_ptr_array_add (self->master_deck, g_object_ref (instance));
    lrg_card_pile_add_top (self->discard_pile, instance);

    g_signal_emit (self, signals[SIGNAL_CARD_ADDED], 0, instance);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOTAL_CARDS]);

    lrg_log_debug ("Added card '%s' to deck", lrg_card_def_get_id (card_def));
}

/**
 * lrg_deck_instance_remove_card:
 * @self: a #LrgDeckInstance
 * @card: the card to remove
 *
 * Permanently removes a card from the deck.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
gboolean
lrg_deck_instance_remove_card (LrgDeckInstance *self,
                               LrgCardInstance *card)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    removed = FALSE;

    /* Try to remove from each location */
    if (lrg_card_pile_remove (self->draw_pile, card))
        removed = TRUE;
    else if (lrg_card_pile_remove (self->discard_pile, card))
        removed = TRUE;
    else if (lrg_card_pile_remove (self->exhaust_pile, card))
        removed = TRUE;
    else if (lrg_hand_remove (self->hand, card))
        removed = TRUE;

    if (removed)
    {
        g_ptr_array_remove (self->master_deck, card);
        g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOTAL_CARDS]);

        lrg_log_debug ("Removed card '%s' from deck",
                       lrg_card_instance_get_id (card));
    }

    return removed;
}

/**
 * lrg_deck_instance_upgrade_card:
 * @self: a #LrgDeckInstance
 * @card: the card to upgrade
 *
 * Upgrades a card in the deck.
 *
 * Returns: %TRUE if upgraded
 *
 * Since: 1.0
 */
gboolean
lrg_deck_instance_upgrade_card (LrgDeckInstance *self,
                                LrgCardInstance *card)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    return lrg_card_instance_upgrade (card);
}

/**
 * lrg_deck_instance_transform_card:
 * @self: a #LrgDeckInstance
 * @old_card: the card to transform
 * @new_card_def: the new card definition
 *
 * Transforms a card into a different card.
 *
 * Returns: %TRUE if transformed
 *
 * Since: 1.0
 */
gboolean
lrg_deck_instance_transform_card (LrgDeckInstance *self,
                                  LrgCardInstance *old_card,
                                  LrgCardDef      *new_card_def)
{
    LrgCardInstance *new_card;
    LrgCardZone zone;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (old_card), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (new_card_def), FALSE);

    zone = lrg_card_instance_get_zone (old_card);

    /* Remove old card */
    if (!lrg_deck_instance_remove_card (self, old_card))
        return FALSE;

    /* Create and add new card */
    new_card = lrg_card_instance_new (new_card_def);
    g_ptr_array_add (self->master_deck, g_object_ref (new_card));

    /* Add to same zone */
    switch (zone)
    {
    case LRG_ZONE_DRAW:
        lrg_card_pile_add_top (self->draw_pile, new_card);
        break;
    case LRG_ZONE_DISCARD:
        lrg_card_pile_add_top (self->discard_pile, new_card);
        break;
    case LRG_ZONE_HAND:
        lrg_hand_add (self->hand, new_card);
        break;
    default:
        lrg_card_pile_add_top (self->discard_pile, new_card);
        break;
    }

    g_signal_emit (self, signals[SIGNAL_CARD_ADDED], 0, new_card);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOTAL_CARDS]);

    lrg_log_debug ("Transformed card to '%s'", lrg_card_def_get_id (new_card_def));

    return TRUE;
}

/**
 * lrg_deck_instance_get_total_cards:
 * @self: a #LrgDeckInstance
 *
 * Gets the total number of cards in the deck.
 *
 * Returns: total card count
 *
 * Since: 1.0
 */
guint
lrg_deck_instance_get_total_cards (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), 0);

    return self->master_deck->len;
}

/**
 * lrg_deck_instance_count_card_def:
 * @self: a #LrgDeckInstance
 * @card_def: the card definition
 *
 * Counts how many copies of a card def are in the deck.
 *
 * Returns: number of copies
 *
 * Since: 1.0
 */
guint
lrg_deck_instance_count_card_def (LrgDeckInstance *self,
                                  LrgCardDef      *card_def)
{
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), 0);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), 0);

    count = 0;
    for (i = 0; i < self->master_deck->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->master_deck, i);
        if (lrg_card_instance_get_def (card) == card_def)
            count++;
    }

    return count;
}

/**
 * lrg_deck_instance_get_all_cards:
 * @self: a #LrgDeckInstance
 *
 * Gets all cards in the deck.
 *
 * Returns: (element-type LrgCardInstance) (transfer container): all cards
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deck_instance_get_all_cards (LrgDeckInstance *self)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    result = g_ptr_array_new ();
    for (i = 0; i < self->master_deck->len; i++)
    {
        g_ptr_array_add (result, g_ptr_array_index (self->master_deck, i));
    }

    return result;
}

/**
 * lrg_deck_instance_find_cards_by_def:
 * @self: a #LrgDeckInstance
 * @card_def: the card definition
 *
 * Finds all cards with a specific definition.
 *
 * Returns: (element-type LrgCardInstance) (transfer container): matching cards
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deck_instance_find_cards_by_def (LrgDeckInstance *self,
                                     LrgCardDef      *card_def)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), NULL);

    result = g_ptr_array_new ();
    for (i = 0; i < self->master_deck->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->master_deck, i);
        if (lrg_card_instance_get_def (card) == card_def)
            g_ptr_array_add (result, card);
    }

    return result;
}

/**
 * lrg_deck_instance_get_master_deck:
 * @self: a #LrgDeckInstance
 *
 * Gets the master deck (all cards in the run).
 *
 * Returns: (element-type LrgCardInstance) (transfer none): the master deck
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deck_instance_get_master_deck (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), NULL);

    return self->master_deck;
}

/**
 * lrg_deck_instance_get_master_deck_size:
 * @self: a #LrgDeckInstance
 *
 * Gets the master deck size.
 *
 * Returns: number of cards
 *
 * Since: 1.0
 */
guint
lrg_deck_instance_get_master_deck_size (LrgDeckInstance *self)
{
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (self), 0);

    return self->master_deck->len;
}
