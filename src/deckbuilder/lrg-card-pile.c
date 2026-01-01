/* lrg-card-pile.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-card-pile.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgCardPile:
 *
 * A pile of cards (draw, discard, exhaust).
 *
 * Internal storage: array where index 0 is bottom, len-1 is top.
 * Drawing from top = removing from end = O(1).
 */
struct _LrgCardPile
{
    GObject parent_instance;

    GPtrArray   *cards;
    LrgCardZone  zone;
};

G_DEFINE_TYPE (LrgCardPile, lrg_card_pile, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_COUNT,
    PROP_ZONE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CARD_ADDED,
    SIGNAL_CARD_REMOVED,
    SIGNAL_SHUFFLED,
    SIGNAL_CLEARED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_card_pile_finalize (GObject *object)
{
    LrgCardPile *self = LRG_CARD_PILE (object);

    g_clear_pointer (&self->cards, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_card_pile_parent_class)->finalize (object);
}

static void
lrg_card_pile_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgCardPile *self = LRG_CARD_PILE (object);

    switch (prop_id)
    {
    case PROP_COUNT:
        g_value_set_uint (value, self->cards->len);
        break;
    case PROP_ZONE:
        g_value_set_enum (value, self->zone);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_pile_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgCardPile *self = LRG_CARD_PILE (object);

    switch (prop_id)
    {
    case PROP_ZONE:
        self->zone = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_pile_class_init (LrgCardPileClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_card_pile_finalize;
    object_class->get_property = lrg_card_pile_get_property;
    object_class->set_property = lrg_card_pile_set_property;

    /**
     * LrgCardPile:count:
     *
     * The number of cards in the pile.
     *
     * Since: 1.0
     */
    properties[PROP_COUNT] =
        g_param_spec_uint ("count",
                           "Count",
                           "The number of cards in the pile",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardPile:zone:
     *
     * The zone for cards in this pile.
     *
     * Since: 1.0
     */
    properties[PROP_ZONE] =
        g_param_spec_enum ("zone",
                           "Zone",
                           "The zone for cards in this pile",
                           LRG_TYPE_CARD_ZONE,
                           LRG_ZONE_LIMBO,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgCardPile::card-added:
     * @self: the pile
     * @card: the added card
     *
     * Emitted when a card is added to the pile.
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
     * LrgCardPile::card-removed:
     * @self: the pile
     * @card: the removed card
     *
     * Emitted when a card is removed from the pile.
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
     * LrgCardPile::shuffled:
     * @self: the pile
     *
     * Emitted when the pile is shuffled.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SHUFFLED] =
        g_signal_new ("shuffled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgCardPile::cleared:
     * @self: the pile
     *
     * Emitted when the pile is cleared.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CLEARED] =
        g_signal_new ("cleared",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_card_pile_init (LrgCardPile *self)
{
    self->cards = g_ptr_array_new_with_free_func (g_object_unref);
    self->zone = LRG_ZONE_LIMBO;
}

/* Public API */

LrgCardPile *
lrg_card_pile_new (void)
{
    return g_object_new (LRG_TYPE_CARD_PILE, NULL);
}

LrgCardPile *
lrg_card_pile_new_with_zone (LrgCardZone zone)
{
    return g_object_new (LRG_TYPE_CARD_PILE,
                         "zone", zone,
                         NULL);
}

void
lrg_card_pile_add (LrgCardPile     *self,
                   LrgCardInstance *card,
                   LrgPilePosition  position)
{
    guint index;

    g_return_if_fail (LRG_IS_CARD_PILE (self));
    g_return_if_fail (LRG_IS_CARD_INSTANCE (card));

    /* Set the card's zone */
    lrg_card_instance_set_zone (card, self->zone);

    switch (position)
    {
    case LRG_PILE_POSITION_TOP:
        /* Add to end (top of pile) */
        g_ptr_array_add (self->cards, card);
        break;

    case LRG_PILE_POSITION_BOTTOM:
        /* Insert at beginning (bottom of pile) */
        g_ptr_array_insert (self->cards, 0, card);
        break;

    case LRG_PILE_POSITION_RANDOM:
        /* Insert at random position */
        if (self->cards->len == 0)
        {
            g_ptr_array_add (self->cards, card);
        }
        else
        {
            index = g_random_int_range (0, self->cards->len + 1);
            if (index == self->cards->len)
                g_ptr_array_add (self->cards, card);
            else
                g_ptr_array_insert (self->cards, index, card);
        }
        break;

    default:
        g_ptr_array_add (self->cards, card);
        break;
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_ADDED], 0, card);

    lrg_log_debug ("Added card '%s' to pile (%u cards)",
                   lrg_card_instance_get_id (card),
                   self->cards->len);
}

void
lrg_card_pile_add_top (LrgCardPile     *self,
                       LrgCardInstance *card)
{
    lrg_card_pile_add (self, card, LRG_PILE_POSITION_TOP);
}

void
lrg_card_pile_add_bottom (LrgCardPile     *self,
                          LrgCardInstance *card)
{
    lrg_card_pile_add (self, card, LRG_PILE_POSITION_BOTTOM);
}

LrgCardInstance *
lrg_card_pile_draw (LrgCardPile *self)
{
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    if (self->cards->len == 0)
        return NULL;

    /* Remove from end (top of pile) - O(1) */
    card = g_ptr_array_steal_index (self->cards, self->cards->len - 1);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);

    lrg_log_debug ("Drew card '%s' from pile (%u remaining)",
                   lrg_card_instance_get_id (card),
                   self->cards->len);

    return card;
}

LrgCardInstance *
lrg_card_pile_draw_bottom (LrgCardPile *self)
{
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    if (self->cards->len == 0)
        return NULL;

    /* Remove from beginning (bottom of pile) */
    card = g_ptr_array_steal_index (self->cards, 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);

    return card;
}

LrgCardInstance *
lrg_card_pile_draw_random (LrgCardPile *self,
                           GRand       *rng)
{
    LrgCardInstance *card;
    guint index;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    if (self->cards->len == 0)
        return NULL;

    if (rng != NULL)
        index = g_rand_int_range (rng, 0, self->cards->len);
    else
        index = g_random_int_range (0, self->cards->len);

    card = g_ptr_array_steal_index (self->cards, index);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);

    return card;
}

gboolean
lrg_card_pile_remove (LrgCardPile     *self,
                      LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
        {
            /*
             * Use steal_index to remove without triggering free function.
             * Caller retains original ownership of the card.
             */
            g_ptr_array_steal_index (self->cards, i);
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
            g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);
            return TRUE;
        }
    }

    return FALSE;
}

LrgCardInstance *
lrg_card_pile_peek (LrgCardPile *self)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    if (self->cards->len == 0)
        return NULL;

    return g_ptr_array_index (self->cards, self->cards->len - 1);
}

GPtrArray *
lrg_card_pile_peek_n (LrgCardPile *self,
                      guint        n)
{
    GPtrArray *result;
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    result = g_ptr_array_new ();
    count = MIN (n, self->cards->len);

    /* Peek from top (end of array) */
    for (i = 0; i < count; i++)
    {
        LrgCardInstance *card;
        card = g_ptr_array_index (self->cards, self->cards->len - 1 - i);
        g_ptr_array_add (result, card);
    }

    return result;
}

void
lrg_card_pile_shuffle (LrgCardPile *self,
                       GRand       *rng)
{
    guint i;
    guint j;
    gpointer temp;

    g_return_if_fail (LRG_IS_CARD_PILE (self));

    if (self->cards->len <= 1)
        return;

    /*
     * Fisher-Yates shuffle algorithm.
     * Iterates from end to beginning, swapping each element with
     * a random element from the remaining unshuffled portion.
     */
    for (i = self->cards->len - 1; i > 0; i--)
    {
        if (rng != NULL)
            j = g_rand_int_range (rng, 0, i + 1);
        else
            j = g_random_int_range (0, i + 1);

        /* Swap elements at i and j */
        temp = g_ptr_array_index (self->cards, i);
        g_ptr_array_index (self->cards, i) = g_ptr_array_index (self->cards, j);
        g_ptr_array_index (self->cards, j) = temp;
    }

    g_signal_emit (self, signals[SIGNAL_SHUFFLED], 0);

    lrg_log_debug ("Shuffled pile (%u cards)", self->cards->len);
}

guint
lrg_card_pile_get_count (LrgCardPile *self)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), 0);

    return self->cards->len;
}

gboolean
lrg_card_pile_is_empty (LrgCardPile *self)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), TRUE);

    return self->cards->len == 0;
}

gboolean
lrg_card_pile_contains (LrgCardPile     *self,
                        LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
            return TRUE;
    }

    return FALSE;
}

LrgCardInstance *
lrg_card_pile_get_card_at (LrgCardPile *self,
                           guint        index)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    if (index >= self->cards->len)
        return NULL;

    return g_ptr_array_index (self->cards, index);
}

GPtrArray *
lrg_card_pile_get_cards (LrgCardPile *self)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    return self->cards;
}

guint
lrg_card_pile_transfer_all (LrgCardPile *self,
                            LrgCardPile *dest)
{
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), 0);
    g_return_val_if_fail (LRG_IS_CARD_PILE (dest), 0);
    g_return_val_if_fail (self != dest, 0);

    count = self->cards->len;

    for (i = 0; i < count; i++)
    {
        LrgCardInstance *card;
        card = g_ptr_array_index (self->cards, i);
        g_object_ref (card);
        lrg_card_pile_add_top (dest, card);
    }

    g_ptr_array_set_size (self->cards, 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);

    lrg_log_debug ("Transferred %u cards between piles", count);

    return count;
}

void
lrg_card_pile_clear (LrgCardPile *self)
{
    g_return_if_fail (LRG_IS_CARD_PILE (self));

    if (self->cards->len > 0)
    {
        g_ptr_array_set_size (self->cards, 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
        g_signal_emit (self, signals[SIGNAL_CLEARED], 0);
    }
}

LrgCardZone
lrg_card_pile_get_zone (LrgCardPile *self)
{
    g_return_val_if_fail (LRG_IS_CARD_PILE (self), LRG_ZONE_DRAW);

    return self->zone;
}

void
lrg_card_pile_set_zone (LrgCardPile *self,
                        LrgCardZone  zone)
{
    g_return_if_fail (LRG_IS_CARD_PILE (self));

    if (self->zone != zone)
    {
        self->zone = zone;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZONE]);
    }
}

LrgCardInstance *
lrg_card_pile_find_by_id (LrgCardPile *self,
                          const gchar *card_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);
    g_return_val_if_fail (card_id != NULL, NULL);

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        const gchar *id = lrg_card_instance_get_id (card);

        if (g_strcmp0 (id, card_id) == 0)
            return card;
    }

    return NULL;
}

GPtrArray *
lrg_card_pile_find_all_by_id (LrgCardPile *self,
                              const gchar *card_id)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);
    g_return_val_if_fail (card_id != NULL, NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        const gchar *id = lrg_card_instance_get_id (card);

        if (g_strcmp0 (id, card_id) == 0)
            g_ptr_array_add (result, card);
    }

    return result;
}

GPtrArray *
lrg_card_pile_find_by_type (LrgCardPile *self,
                            LrgCardType  card_type)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        LrgCardDef *def = lrg_card_instance_get_def (card);

        if (def != NULL && lrg_card_def_get_card_type (def) == card_type)
            g_ptr_array_add (result, card);
    }

    return result;
}

GPtrArray *
lrg_card_pile_find_by_keyword (LrgCardPile    *self,
                               LrgCardKeyword  keyword)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_PILE (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);

        if (lrg_card_instance_has_keyword (card, keyword))
            g_ptr_array_add (result, card);
    }

    return result;
}

void
lrg_card_pile_foreach (LrgCardPile *self,
                       GFunc        func,
                       gpointer     user_data)
{
    guint i;

    g_return_if_fail (LRG_IS_CARD_PILE (self));
    g_return_if_fail (func != NULL);

    for (i = 0; i < self->cards->len; i++)
    {
        func (g_ptr_array_index (self->cards, i), user_data);
    }
}
