/* lrg-hand.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-hand.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgHand:
 *
 * The player's hand of cards during gameplay.
 *
 * The hand has a maximum size and manages adding/removing cards.
 * It respects the Retain keyword when discarding at end of turn.
 */
struct _LrgHand
{
    GObject parent_instance;

    GPtrArray *cards;       /* Array of LrgCardInstance */
    GPtrArray *selected;    /* Array of selected LrgCardInstance for UI */
    guint max_size;
};

G_DEFINE_TYPE (LrgHand, lrg_hand, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_COUNT,
    PROP_MAX_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CARD_ADDED,
    SIGNAL_CARD_REMOVED,
    SIGNAL_CARD_DISCARDED,
    SIGNAL_CARD_RETAINED,
    SIGNAL_SELECTION_CHANGED,
    SIGNAL_CLEARED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Forward declarations for sort functions */
static gint compare_cards_by_cost_asc (gconstpointer a, gconstpointer b);
static gint compare_cards_by_cost_desc (gconstpointer a, gconstpointer b);
static gint compare_cards_by_type (gconstpointer a, gconstpointer b);

static void
lrg_hand_dispose (GObject *object)
{
    LrgHand *self = LRG_HAND (object);

    if (self->cards != NULL)
    {
        g_ptr_array_unref (self->cards);
        self->cards = NULL;
    }

    if (self->selected != NULL)
    {
        g_ptr_array_unref (self->selected);
        self->selected = NULL;
    }

    G_OBJECT_CLASS (lrg_hand_parent_class)->dispose (object);
}

static void
lrg_hand_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgHand *self = LRG_HAND (object);

    switch (prop_id)
    {
    case PROP_COUNT:
        g_value_set_uint (value, self->cards->len);
        break;
    case PROP_MAX_SIZE:
        g_value_set_uint (value, self->max_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_hand_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgHand *self = LRG_HAND (object);

    switch (prop_id)
    {
    case PROP_MAX_SIZE:
        self->max_size = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_hand_class_init (LrgHandClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_hand_dispose;
    object_class->get_property = lrg_hand_get_property;
    object_class->set_property = lrg_hand_set_property;

    /**
     * LrgHand:count:
     *
     * The number of cards currently in the hand.
     *
     * Since: 1.0
     */
    properties[PROP_COUNT] =
        g_param_spec_uint ("count", NULL, NULL,
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgHand:max-size:
     *
     * The maximum number of cards the hand can hold.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_SIZE] =
        g_param_spec_uint ("max-size", NULL, NULL,
                           1, G_MAXUINT, LRG_HAND_DEFAULT_MAX_SIZE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgHand::card-added:
     * @self: the hand
     * @card: the card that was added
     *
     * Emitted when a card is added to the hand.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_ADDED] =
        g_signal_new ("card-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgHand::card-removed:
     * @self: the hand
     * @card: the card that was removed
     *
     * Emitted when a card is removed from the hand.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_REMOVED] =
        g_signal_new ("card-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgHand::card-discarded:
     * @self: the hand
     * @card: the card that was discarded
     *
     * Emitted when a card is discarded from the hand.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_DISCARDED] =
        g_signal_new ("card-discarded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgHand::card-retained:
     * @self: the hand
     * @card: the card that was retained
     *
     * Emitted when a card with Retain keyword stays in hand during discard.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CARD_RETAINED] =
        g_signal_new ("card-retained",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CARD_INSTANCE);

    /**
     * LrgHand::selection-changed:
     * @self: the hand
     *
     * Emitted when the card selection changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SELECTION_CHANGED] =
        g_signal_new ("selection-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgHand::cleared:
     * @self: the hand
     *
     * Emitted when the hand is cleared.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CLEARED] =
        g_signal_new ("cleared",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_hand_init (LrgHand *self)
{
    self->cards = g_ptr_array_new_with_free_func (g_object_unref);
    self->selected = g_ptr_array_new ();  /* No free func - doesn't own refs */
    self->max_size = LRG_HAND_DEFAULT_MAX_SIZE;
}

/* Construction */

LrgHand *
lrg_hand_new (void)
{
    return g_object_new (LRG_TYPE_HAND, NULL);
}

LrgHand *
lrg_hand_new_with_size (guint max_size)
{
    return g_object_new (LRG_TYPE_HAND,
                         "max-size", max_size,
                         NULL);
}

/* Card Operations */

gboolean
lrg_hand_add (LrgHand         *self,
              LrgCardInstance *card)
{
    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    if (self->cards->len >= self->max_size)
    {
        lrg_log_debug ("Hand full (%u/%u), cannot add card",
                       self->cards->len, self->max_size);
        return FALSE;
    }

    /* Takes ownership of the card reference */
    g_ptr_array_add (self->cards, card);
    lrg_card_instance_set_zone (card, LRG_ZONE_HAND);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_ADDED], 0, card);

    return TRUE;
}

LrgCardInstance *
lrg_hand_remove (LrgHand         *self,
                 LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), NULL);

    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
        {
            /* Ref before stealing to return to caller */
            g_object_ref (card);
            g_ptr_array_remove_index (self->cards, i);

            /* Remove from selection if selected */
            g_ptr_array_remove (self->selected, card);

            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
            g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);

            return card;
        }
    }

    return NULL;
}

LrgCardInstance *
lrg_hand_remove_at (LrgHand *self,
                    guint    index)
{
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);

    if (index >= self->cards->len)
        return NULL;

    card = g_ptr_array_index (self->cards, index);
    g_object_ref (card);
    g_ptr_array_remove_index (self->cards, index);

    /* Remove from selection if selected */
    g_ptr_array_remove (self->selected, card);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_REMOVED], 0, card);

    return card;
}

gboolean
lrg_hand_discard (LrgHand         *self,
                  LrgCardInstance *card,
                  LrgCardPile     *discard_pile)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_PILE (discard_pile), FALSE);

    /* Check for Retain keyword */
    if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_RETAIN))
    {
        g_signal_emit (self, signals[SIGNAL_CARD_RETAINED], 0, card);
        return FALSE;
    }

    /* Remove and transfer to discard pile */
    if (!lrg_hand_contains (self, card))
        return FALSE;

    /* Find and remove, but don't unref yet - pile will take ownership */
    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
        {
            g_object_ref (card);  /* Ref for the pile */
            g_ptr_array_remove_index (self->cards, i);
            break;
        }
    }

    /* Remove from selection if selected */
    g_ptr_array_remove (self->selected, card);

    /* Add to discard pile (takes ownership) */
    lrg_card_pile_add (discard_pile, card, LRG_PILE_POSITION_TOP);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
    g_signal_emit (self, signals[SIGNAL_CARD_DISCARDED], 0, card);

    return TRUE;
}

guint
lrg_hand_discard_all (LrgHand     *self,
                      LrgCardPile *discard_pile)
{
    guint count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), 0);
    g_return_val_if_fail (LRG_IS_CARD_PILE (discard_pile), 0);

    /*
     * Iterate backwards to safely remove while iterating.
     * Check Retain keyword for each card.
     */
    for (i = self->cards->len; i > 0; i--)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i - 1);

        /* Retain keyword: keep in hand */
        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_RETAIN))
        {
            g_signal_emit (self, signals[SIGNAL_CARD_RETAINED], 0, card);
            continue;
        }

        /* Discard the card */
        g_object_ref (card);
        g_ptr_array_remove_index (self->cards, i - 1);

        /* Remove from selection */
        g_ptr_array_remove (self->selected, card);

        /* Add to discard pile (takes ownership) */
        lrg_card_pile_add (discard_pile, card, LRG_PILE_POSITION_TOP);

        g_signal_emit (self, signals[SIGNAL_CARD_DISCARDED], 0, card);
        count++;
    }

    if (count > 0)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);

    return count;
}

LrgCardInstance *
lrg_hand_discard_random (LrgHand     *self,
                         LrgCardPile *discard_pile,
                         GRand       *rng)
{
    GPtrArray *discardable;
    LrgCardInstance *card;
    guint i;
    guint index;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
    g_return_val_if_fail (LRG_IS_CARD_PILE (discard_pile), NULL);

    if (self->cards->len == 0)
        return NULL;

    /* Build list of discardable cards (excluding Retain) */
    discardable = g_ptr_array_new ();
    for (i = 0; i < self->cards->len; i++)
    {
        card = g_ptr_array_index (self->cards, i);
        if (!lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_RETAIN))
            g_ptr_array_add (discardable, card);
    }

    if (discardable->len == 0)
    {
        g_ptr_array_unref (discardable);
        return NULL;
    }

    /* Pick random card */
    if (rng != NULL)
        index = g_rand_int_range (rng, 0, discardable->len);
    else
        index = g_random_int_range (0, discardable->len);

    card = g_ptr_array_index (discardable, index);
    g_ptr_array_unref (discardable);

    /* Discard the selected card */
    lrg_hand_discard (self, card, discard_pile);

    return card;
}

/* Query */

guint
lrg_hand_get_count (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), 0);
    return self->cards->len;
}

guint
lrg_hand_get_max_size (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), 0);
    return self->max_size;
}

void
lrg_hand_set_max_size (LrgHand *self,
                       guint    max_size)
{
    g_return_if_fail (LRG_IS_HAND (self));
    g_return_if_fail (max_size > 0);

    if (self->max_size != max_size)
    {
        self->max_size = max_size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_SIZE]);
    }
}

gboolean
lrg_hand_is_full (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), TRUE);
    return self->cards->len >= self->max_size;
}

gboolean
lrg_hand_is_empty (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), TRUE);
    return self->cards->len == 0;
}

gboolean
lrg_hand_contains (LrgHand         *self,
                   LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
            return TRUE;
    }

    return FALSE;
}

LrgCardInstance *
lrg_hand_get_card_at (LrgHand *self,
                      guint    index)
{
    g_return_val_if_fail (LRG_IS_HAND (self), NULL);

    if (index >= self->cards->len)
        return NULL;

    return g_ptr_array_index (self->cards, index);
}

GPtrArray *
lrg_hand_get_cards (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
    return self->cards;
}

/* Search */

LrgCardInstance *
lrg_hand_find_by_id (LrgHand     *self,
                     const gchar *card_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
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
lrg_hand_find_all_by_id (LrgHand     *self,
                         const gchar *card_id)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
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
lrg_hand_find_by_type (LrgHand     *self,
                       LrgCardType  card_type)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        LrgCardDef *def = lrg_card_instance_get_def (card);
        if (lrg_card_def_get_card_type (def) == card_type)
            g_ptr_array_add (result, card);
    }

    return result;
}

GPtrArray *
lrg_hand_find_by_keyword (LrgHand        *self,
                          LrgCardKeyword  keyword)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        if (lrg_card_instance_has_keyword (card, keyword))
            g_ptr_array_add (result, card);
    }

    return result;
}

GPtrArray *
lrg_hand_find_playable (LrgHand *self,
                        gint     available_energy)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (self->cards, i);
        LrgCardDef *def = lrg_card_instance_get_def (card);
        gint cost;

        /* Skip unplayable cards */
        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_UNPLAYABLE))
            continue;

        /* Check cost (X-cost cards are always "playable" with 0+ energy) */
        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_X_COST))
        {
            g_ptr_array_add (result, card);
            continue;
        }

        cost = lrg_card_def_get_base_cost (def);
        cost += lrg_card_instance_get_cost_modifier (card);
        if (cost <= available_energy)
            g_ptr_array_add (result, card);
    }

    return result;
}

/* Selection Support */

GPtrArray *
lrg_hand_get_selected (LrgHand *self)
{
    g_return_val_if_fail (LRG_IS_HAND (self), NULL);
    return self->selected;
}

gboolean
lrg_hand_select (LrgHand         *self,
                 LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    /* Must be in hand */
    if (!lrg_hand_contains (self, card))
        return FALSE;

    /* Check if already selected */
    for (i = 0; i < self->selected->len; i++)
    {
        if (g_ptr_array_index (self->selected, i) == card)
            return FALSE;  /* Already selected */
    }

    g_ptr_array_add (self->selected, card);
    g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);

    return TRUE;
}

gboolean
lrg_hand_deselect (LrgHand         *self,
                   LrgCardInstance *card)
{
    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    if (g_ptr_array_remove (self->selected, card))
    {
        g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
        return TRUE;
    }

    return FALSE;
}

void
lrg_hand_clear_selection (LrgHand *self)
{
    g_return_if_fail (LRG_IS_HAND (self));

    if (self->selected->len > 0)
    {
        g_ptr_array_set_size (self->selected, 0);
        g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
    }
}

gboolean
lrg_hand_is_selected (LrgHand         *self,
                      LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    for (i = 0; i < self->selected->len; i++)
    {
        if (g_ptr_array_index (self->selected, i) == card)
            return TRUE;
    }

    return FALSE;
}

/* Utility */

void
lrg_hand_clear (LrgHand *self)
{
    g_return_if_fail (LRG_IS_HAND (self));

    if (self->cards->len > 0)
    {
        g_ptr_array_set_size (self->cards, 0);
        g_ptr_array_set_size (self->selected, 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
        g_signal_emit (self, signals[SIGNAL_CLEARED], 0);
    }
}

void
lrg_hand_foreach (LrgHand  *self,
                  GFunc     func,
                  gpointer  user_data)
{
    g_return_if_fail (LRG_IS_HAND (self));
    g_return_if_fail (func != NULL);

    g_ptr_array_foreach (self->cards, func, user_data);
}

gint
lrg_hand_get_index_of (LrgHand         *self,
                       LrgCardInstance *card)
{
    guint i;

    g_return_val_if_fail (LRG_IS_HAND (self), -1);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), -1);

    for (i = 0; i < self->cards->len; i++)
    {
        if (g_ptr_array_index (self->cards, i) == card)
            return (gint)i;
    }

    return -1;
}

static gint
compare_cards_by_cost_asc (gconstpointer a,
                           gconstpointer b)
{
    LrgCardInstance *card_a = *(LrgCardInstance **)a;
    LrgCardInstance *card_b = *(LrgCardInstance **)b;
    LrgCardDef *def_a = lrg_card_instance_get_def (card_a);
    LrgCardDef *def_b = lrg_card_instance_get_def (card_b);
    gint cost_a = lrg_card_def_get_base_cost (def_a);
    gint cost_b = lrg_card_def_get_base_cost (def_b);

    cost_a += lrg_card_instance_get_cost_modifier (card_a);
    cost_b += lrg_card_instance_get_cost_modifier (card_b);

    return cost_a - cost_b;
}

static gint
compare_cards_by_cost_desc (gconstpointer a,
                            gconstpointer b)
{
    return -compare_cards_by_cost_asc (a, b);
}

static gint
compare_cards_by_type (gconstpointer a,
                       gconstpointer b)
{
    LrgCardInstance *card_a = *(LrgCardInstance **)a;
    LrgCardInstance *card_b = *(LrgCardInstance **)b;
    LrgCardDef *def_a = lrg_card_instance_get_def (card_a);
    LrgCardDef *def_b = lrg_card_instance_get_def (card_b);
    LrgCardType type_a = lrg_card_def_get_card_type (def_a);
    LrgCardType type_b = lrg_card_def_get_card_type (def_b);

    return (gint)type_a - (gint)type_b;
}

void
lrg_hand_sort_by_cost (LrgHand  *self,
                       gboolean  ascending)
{
    g_return_if_fail (LRG_IS_HAND (self));

    if (self->cards->len < 2)
        return;

    if (ascending)
        g_ptr_array_sort (self->cards, compare_cards_by_cost_asc);
    else
        g_ptr_array_sort (self->cards, compare_cards_by_cost_desc);
}

void
lrg_hand_sort_by_type (LrgHand *self)
{
    g_return_if_fail (LRG_IS_HAND (self));

    if (self->cards->len < 2)
        return;

    g_ptr_array_sort (self->cards, compare_cards_by_type);
}
