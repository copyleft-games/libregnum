/* lrg-deck-builder.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-deck-builder.h"
#include "lrg-deck-def.h"
#include "lrg-deck-instance.h"
#include "lrg-card-def.h"
#include "lrg-card-instance.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgDeckBuilder:
 *
 * Utility for constructing and validating decks.
 *
 * Provides validation against deck definition constraints,
 * card limits, and construction helpers.
 */

struct _LrgDeckBuilder
{
    GObject parent_instance;

    LrgDeckDef *deck_def;
    guint       max_copies;  /* Per-card limit, 0 = unlimited */
};

G_DEFINE_TYPE (LrgDeckBuilder, lrg_deck_builder, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_DECK_DEF,
    PROP_MAX_COPIES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject implementation
 * ========================================================================== */

static void
lrg_deck_builder_finalize (GObject *object)
{
    LrgDeckBuilder *self = LRG_DECK_BUILDER (object);

    g_clear_object (&self->deck_def);

    G_OBJECT_CLASS (lrg_deck_builder_parent_class)->finalize (object);
}

static void
lrg_deck_builder_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgDeckBuilder *self = LRG_DECK_BUILDER (object);

    switch (prop_id)
    {
    case PROP_DECK_DEF:
        g_value_set_object (value, self->deck_def);
        break;
    case PROP_MAX_COPIES:
        g_value_set_uint (value, self->max_copies);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_builder_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgDeckBuilder *self = LRG_DECK_BUILDER (object);

    switch (prop_id)
    {
    case PROP_DECK_DEF:
        g_clear_object (&self->deck_def);
        self->deck_def = g_value_dup_object (value);
        break;
    case PROP_MAX_COPIES:
        self->max_copies = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_builder_class_init (LrgDeckBuilderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_deck_builder_finalize;
    object_class->get_property = lrg_deck_builder_get_property;
    object_class->set_property = lrg_deck_builder_set_property;

    /**
     * LrgDeckBuilder:deck-def:
     *
     * The deck definition to build from.
     *
     * Since: 1.0
     */
    properties[PROP_DECK_DEF] =
        g_param_spec_object ("deck-def",
                             "Deck Definition",
                             "The deck definition",
                             LRG_TYPE_DECK_DEF,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckBuilder:max-copies:
     *
     * Maximum copies of any single card allowed.
     * Set to 0 for no limit.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_COPIES] =
        g_param_spec_uint ("max-copies",
                           "Max Copies",
                           "Maximum copies per card",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_deck_builder_init (LrgDeckBuilder *self)
{
    self->max_copies = 0;  /* Unlimited by default */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_deck_builder_new:
 *
 * Creates a new deck builder.
 *
 * Returns: (transfer full): a new #LrgDeckBuilder
 *
 * Since: 1.0
 */
LrgDeckBuilder *
lrg_deck_builder_new (void)
{
    return g_object_new (LRG_TYPE_DECK_BUILDER, NULL);
}

/**
 * lrg_deck_builder_new_with_def:
 * @deck_def: the deck definition
 *
 * Creates a new deck builder with a definition.
 *
 * Returns: (transfer full): a new #LrgDeckBuilder
 *
 * Since: 1.0
 */
LrgDeckBuilder *
lrg_deck_builder_new_with_def (LrgDeckDef *deck_def)
{
    g_return_val_if_fail (LRG_IS_DECK_DEF (deck_def), NULL);

    return g_object_new (LRG_TYPE_DECK_BUILDER,
                         "deck-def", deck_def,
                         NULL);
}

/**
 * lrg_deck_builder_set_deck_def:
 * @self: a #LrgDeckBuilder
 * @deck_def: (nullable): the deck definition
 *
 * Sets the deck definition.
 *
 * Since: 1.0
 */
void
lrg_deck_builder_set_deck_def (LrgDeckBuilder *self,
                               LrgDeckDef     *deck_def)
{
    g_return_if_fail (LRG_IS_DECK_BUILDER (self));

    if (self->deck_def != deck_def)
    {
        g_clear_object (&self->deck_def);
        if (deck_def != NULL)
            self->deck_def = g_object_ref (deck_def);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DECK_DEF]);
    }
}

/**
 * lrg_deck_builder_get_deck_def:
 * @self: a #LrgDeckBuilder
 *
 * Gets the deck definition.
 *
 * Returns: (transfer none) (nullable): the deck definition
 *
 * Since: 1.0
 */
LrgDeckDef *
lrg_deck_builder_get_deck_def (LrgDeckBuilder *self)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), NULL);

    return self->deck_def;
}

/**
 * lrg_deck_builder_set_max_copies:
 * @self: a #LrgDeckBuilder
 * @max_copies: maximum copies per card (0 = unlimited)
 *
 * Sets the maximum copies limit.
 *
 * Since: 1.0
 */
void
lrg_deck_builder_set_max_copies (LrgDeckBuilder *self,
                                 guint           max_copies)
{
    g_return_if_fail (LRG_IS_DECK_BUILDER (self));

    if (self->max_copies != max_copies)
    {
        self->max_copies = max_copies;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_COPIES]);
    }
}

/**
 * lrg_deck_builder_get_max_copies:
 * @self: a #LrgDeckBuilder
 *
 * Gets the maximum copies limit.
 *
 * Returns: maximum copies (0 = unlimited)
 *
 * Since: 1.0
 */
guint
lrg_deck_builder_get_max_copies (LrgDeckBuilder *self)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), 0);

    return self->max_copies;
}

/**
 * lrg_deck_builder_can_add_card:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @card_def: the card to check
 * @error: (nullable): return location for error
 *
 * Checks if a card can be added to the deck.
 *
 * Returns: %TRUE if card can be added
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_can_add_card (LrgDeckBuilder  *self,
                               LrgDeckInstance *deck,
                               LrgCardDef      *card_def,
                               GError         **error)
{
    LrgCardType card_type;
    guint current_count;
    guint max_size;
    guint max_copies;

    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), FALSE);

    /* Check if card type is allowed */
    if (self->deck_def != NULL)
    {
        card_type = lrg_card_def_get_card_type (card_def);
        if (!lrg_deck_def_is_card_type_allowed (self->deck_def, card_type))
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_CARD_NOT_ALLOWED,
                         "Card type '%d' not allowed in deck", card_type);
            return FALSE;
        }

        /* Check if card is banned */
        if (lrg_deck_def_is_card_banned (self->deck_def, card_def))
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_CARD_BANNED,
                         "Card '%s' is banned from deck",
                         lrg_card_def_get_id (card_def));
            return FALSE;
        }

        /* Check max size */
        max_size = lrg_deck_def_get_max_size (self->deck_def);
        if (max_size > 0 && lrg_deck_instance_get_total_cards (deck) >= max_size)
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE,
                         "Deck already at maximum size (%u)", max_size);
            return FALSE;
        }
    }

    /* Check max copies limit (uses builder's limit, 0 = unlimited) */
    max_copies = self->max_copies;
    if (max_copies > 0)
    {
        current_count = lrg_deck_instance_count_card_def (deck, card_def);
        if (current_count >= max_copies)
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_CARD_LIMIT_EXCEEDED,
                         "Already have %u copies of '%s' (max %u)",
                         current_count,
                         lrg_card_def_get_id (card_def),
                         max_copies);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * lrg_deck_builder_validate_deck:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @error: (nullable): return location for error
 *
 * Validates the deck against all constraints.
 *
 * Returns: %TRUE if valid
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_validate_deck (LrgDeckBuilder  *self,
                                LrgDeckInstance *deck,
                                GError         **error)
{
    guint total_cards;
    guint min_size;
    guint max_size;

    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);

    total_cards = lrg_deck_instance_get_total_cards (deck);

    if (self->deck_def != NULL)
    {
        min_size = lrg_deck_def_get_min_size (self->deck_def);
        max_size = lrg_deck_def_get_max_size (self->deck_def);

        if (min_size > 0 && total_cards < min_size)
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_DECK_TOO_SMALL,
                         "Deck has %u cards, minimum is %u",
                         total_cards, min_size);
            return FALSE;
        }

        if (max_size > 0 && total_cards > max_size)
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE,
                         "Deck has %u cards, maximum is %u",
                         total_cards, max_size);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * lrg_deck_builder_add_card:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @card_def: the card to add
 * @error: (nullable): return location for error
 *
 * Adds a card to the deck if allowed.
 *
 * Returns: %TRUE if added
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_add_card (LrgDeckBuilder  *self,
                           LrgDeckInstance *deck,
                           LrgCardDef      *card_def,
                           GError         **error)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), FALSE);

    if (!lrg_deck_builder_can_add_card (self, deck, card_def, error))
        return FALSE;

    lrg_deck_instance_add_card (deck, card_def);
    return TRUE;
}

/**
 * lrg_deck_builder_remove_card:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @card: the card to remove
 * @error: (nullable): return location for error
 *
 * Removes a card from the deck.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_remove_card (LrgDeckBuilder  *self,
                              LrgDeckInstance *deck,
                              LrgCardInstance *card,
                              GError         **error)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    if (!lrg_deck_instance_remove_card (deck, card))
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_FAILED,
                     "Card not found in deck");
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_deck_builder_upgrade_card:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @card: the card to upgrade
 * @error: (nullable): return location for error
 *
 * Upgrades a card in the deck.
 *
 * Returns: %TRUE if upgraded
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_upgrade_card (LrgDeckBuilder  *self,
                               LrgDeckInstance *deck,
                               LrgCardInstance *card,
                               GError         **error)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    if (!lrg_deck_instance_upgrade_card (deck, card))
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_FAILED,
                     "Card cannot be upgraded");
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_deck_builder_transform_card:
 * @self: a #LrgDeckBuilder
 * @deck: the deck instance
 * @old_card: the card to transform
 * @new_card_def: the new card definition
 * @error: (nullable): return location for error
 *
 * Transforms a card in the deck.
 *
 * Returns: %TRUE if transformed
 *
 * Since: 1.0
 */
gboolean
lrg_deck_builder_transform_card (LrgDeckBuilder  *self,
                                 LrgDeckInstance *deck,
                                 LrgCardInstance *old_card,
                                 LrgCardDef      *new_card_def,
                                 GError         **error)
{
    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), FALSE);
    g_return_val_if_fail (LRG_IS_DECK_INSTANCE (deck), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (old_card), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (new_card_def), FALSE);

    /* Check if new card is allowed */
    if (self->deck_def != NULL)
    {
        LrgCardType card_type = lrg_card_def_get_card_type (new_card_def);
        if (!lrg_deck_def_is_card_type_allowed (self->deck_def, card_type))
        {
            g_set_error (error,
                         LRG_DECKBUILDER_ERROR,
                         LRG_DECKBUILDER_ERROR_CARD_NOT_ALLOWED,
                         "Cannot transform to '%s': type not allowed",
                         lrg_card_def_get_id (new_card_def));
            return FALSE;
        }
    }

    if (!lrg_deck_instance_transform_card (deck, old_card, new_card_def))
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_FAILED,
                     "Failed to transform card");
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_deck_builder_build:
 * @self: a #LrgDeckBuilder
 * @error: (nullable): return location for error
 *
 * Builds a deck instance from the definition.
 *
 * Returns: (transfer full) (nullable): the built deck, or %NULL on error
 *
 * Since: 1.0
 */
LrgDeckInstance *
lrg_deck_builder_build (LrgDeckBuilder *self,
                        GError        **error)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), NULL);

    if (self->deck_def == NULL)
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_FAILED,
                     "No deck definition set");
        return NULL;
    }

    /* Validate definition first */
    if (!lrg_deck_def_validate (self->deck_def, error))
        return NULL;

    deck = lrg_deck_instance_new (self->deck_def);
    lrg_deck_instance_setup (deck);

    lrg_log_debug ("Built deck '%s' with %u cards",
                   lrg_deck_def_get_id (self->deck_def),
                   lrg_deck_instance_get_total_cards (deck));

    return deck;
}

/**
 * lrg_deck_builder_build_with_seed:
 * @self: a #LrgDeckBuilder
 * @seed: the random seed
 * @error: (nullable): return location for error
 *
 * Builds a deck instance with a specific seed.
 *
 * Returns: (transfer full) (nullable): the built deck, or %NULL on error
 *
 * Since: 1.0
 */
LrgDeckInstance *
lrg_deck_builder_build_with_seed (LrgDeckBuilder *self,
                                  guint32         seed,
                                  GError        **error)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_BUILDER (self), NULL);

    if (self->deck_def == NULL)
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_FAILED,
                     "No deck definition set");
        return NULL;
    }

    /* Validate definition first */
    if (!lrg_deck_def_validate (self->deck_def, error))
        return NULL;

    deck = lrg_deck_instance_new_with_seed (self->deck_def, seed);
    lrg_deck_instance_setup (deck);

    lrg_log_debug ("Built deck '%s' with seed %u",
                   lrg_deck_def_get_id (self->deck_def),
                   seed);

    return deck;
}
