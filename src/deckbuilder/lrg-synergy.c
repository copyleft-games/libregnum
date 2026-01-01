/* lrg-synergy.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSynergy - Card synergy definition implementation.
 */

#include "lrg-synergy.h"
#include "../lrg-log.h"

typedef struct
{
    gchar          *id;
    gchar          *name;
    gchar          *description;
    LrgSynergyType  synergy_type;
    guint           min_count;
    gint            bonus_per_card;

    /* Type-specific data */
    LrgCardKeyword  keyword;       /* For KEYWORD type */
    LrgCardType     card_type;     /* For CARD_TYPE type */
    gchar          *tag;           /* For TAG type */
} LrgSynergyPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgSynergy, lrg_synergy, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_SYNERGY_TYPE,
    PROP_MIN_COUNT,
    PROP_BONUS_PER_CARD,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static gboolean lrg_synergy_real_check_cards      (LrgSynergy *self,
                                                   GPtrArray  *cards);
static gint     lrg_synergy_real_calculate_bonus  (LrgSynergy *self,
                                                   GPtrArray  *cards);
static GPtrArray * lrg_synergy_real_get_synergy_cards (LrgSynergy *self,
                                                       GPtrArray  *cards);

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * Count cards matching the synergy criteria.
 * This is used by the default implementations.
 */
static guint
count_matching_cards (LrgSynergy *self,
                      GPtrArray  *cards)
{
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);
    guint count = 0;
    guint i;

    if (cards == NULL)
        return 0;

    for (i = 0; i < cards->len; i++)
    {
        GObject *card = g_ptr_array_index (cards, i);
        gboolean matches = FALSE;

        switch (priv->synergy_type)
        {
        case LRG_SYNERGY_TYPE_KEYWORD:
            {
                LrgCardKeyword keywords = 0;
                g_object_get (card, "keywords", &keywords, NULL);
                matches = (keywords & priv->keyword) != 0;
            }
            break;

        case LRG_SYNERGY_TYPE_CARD_TYPE:
            {
                LrgCardType type = 0;
                g_object_get (card, "card-type", &type, NULL);
                matches = (type == priv->card_type);
            }
            break;

        case LRG_SYNERGY_TYPE_TAG:
            {
                /* Check if card has the tag - implementation depends on card API */
                const gchar *tags = NULL;
                g_object_get (card, "tags", &tags, NULL);
                if (tags != NULL && priv->tag != NULL)
                {
                    matches = (g_strstr_len (tags, -1, priv->tag) != NULL);
                }
            }
            break;

        case LRG_SYNERGY_TYPE_CUSTOM:
            /* Custom synergies must override virtual methods */
            break;
        }

        if (matches)
            count++;
    }

    return count;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_synergy_real_check_cards (LrgSynergy *self,
                              GPtrArray  *cards)
{
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);
    guint count;

    count = count_matching_cards (self, cards);
    return count >= priv->min_count;
}

static gint
lrg_synergy_real_calculate_bonus (LrgSynergy *self,
                                  GPtrArray  *cards)
{
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);
    guint count;
    gint bonus;

    count = count_matching_cards (self, cards);

    if (count < priv->min_count)
        return 0;

    /* Bonus for cards beyond minimum */
    bonus = (gint)(count - priv->min_count + 1) * priv->bonus_per_card;

    return bonus;
}

static GPtrArray *
lrg_synergy_real_get_synergy_cards (LrgSynergy *self,
                                    GPtrArray  *cards)
{
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);
    GPtrArray *result;
    guint i;

    result = g_ptr_array_new ();

    if (cards == NULL)
        return result;

    for (i = 0; i < cards->len; i++)
    {
        GObject *card = g_ptr_array_index (cards, i);
        gboolean matches = FALSE;

        switch (priv->synergy_type)
        {
        case LRG_SYNERGY_TYPE_KEYWORD:
            {
                LrgCardKeyword keywords = 0;
                g_object_get (card, "keywords", &keywords, NULL);
                matches = (keywords & priv->keyword) != 0;
            }
            break;

        case LRG_SYNERGY_TYPE_CARD_TYPE:
            {
                LrgCardType type = 0;
                g_object_get (card, "card-type", &type, NULL);
                matches = (type == priv->card_type);
            }
            break;

        case LRG_SYNERGY_TYPE_TAG:
            {
                const gchar *tags = NULL;
                g_object_get (card, "tags", &tags, NULL);
                if (tags != NULL && priv->tag != NULL)
                {
                    matches = (g_strstr_len (tags, -1, priv->tag) != NULL);
                }
            }
            break;

        case LRG_SYNERGY_TYPE_CUSTOM:
            break;
        }

        if (matches)
            g_ptr_array_add (result, card);
    }

    return result;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_synergy_finalize (GObject *object)
{
    LrgSynergy *self = LRG_SYNERGY (object);
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->tag, g_free);

    G_OBJECT_CLASS (lrg_synergy_parent_class)->finalize (object);
}

static void
lrg_synergy_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgSynergy *self = LRG_SYNERGY (object);
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_SYNERGY_TYPE:
        g_value_set_int (value, priv->synergy_type);
        break;
    case PROP_MIN_COUNT:
        g_value_set_uint (value, priv->min_count);
        break;
    case PROP_BONUS_PER_CARD:
        g_value_set_int (value, priv->bonus_per_card);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_synergy_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgSynergy *self = LRG_SYNERGY (object);
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_clear_pointer (&priv->id, g_free);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_clear_pointer (&priv->name, g_free);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_clear_pointer (&priv->description, g_free);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_SYNERGY_TYPE:
        priv->synergy_type = g_value_get_int (value);
        break;
    case PROP_MIN_COUNT:
        priv->min_count = g_value_get_uint (value);
        break;
    case PROP_BONUS_PER_CARD:
        priv->bonus_per_card = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_synergy_class_init (LrgSynergyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_synergy_finalize;
    object_class->get_property = lrg_synergy_get_property;
    object_class->set_property = lrg_synergy_set_property;

    /* Virtual methods */
    klass->check_cards = lrg_synergy_real_check_cards;
    klass->calculate_bonus = lrg_synergy_real_calculate_bonus;
    klass->get_synergy_cards = lrg_synergy_real_get_synergy_cards;

    /**
     * LrgSynergy:id:
     *
     * The unique identifier for this synergy.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSynergy:name:
     *
     * The display name for this synergy.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSynergy:description:
     *
     * The description for this synergy.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Synergy description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSynergy:synergy-type:
     *
     * The type of synergy.
     *
     * Since: 1.0
     */
    properties[PROP_SYNERGY_TYPE] =
        g_param_spec_int ("synergy-type",
                          "Synergy Type",
                          "Type of synergy",
                          LRG_SYNERGY_TYPE_KEYWORD,
                          LRG_SYNERGY_TYPE_CUSTOM,
                          LRG_SYNERGY_TYPE_KEYWORD,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSynergy:min-count:
     *
     * Minimum card count for synergy activation.
     *
     * Since: 1.0
     */
    properties[PROP_MIN_COUNT] =
        g_param_spec_uint ("min-count",
                           "Minimum Count",
                           "Minimum cards for activation",
                           1, G_MAXUINT, 2,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSynergy:bonus-per-card:
     *
     * Bonus value per additional card.
     *
     * Since: 1.0
     */
    properties[PROP_BONUS_PER_CARD] =
        g_param_spec_int ("bonus-per-card",
                          "Bonus Per Card",
                          "Bonus value per additional card",
                          G_MININT, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_synergy_init (LrgSynergy *self)
{
    LrgSynergyPrivate *priv = lrg_synergy_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->synergy_type = LRG_SYNERGY_TYPE_KEYWORD;
    priv->min_count = 2;
    priv->bonus_per_card = 1;
    priv->keyword = 0;
    priv->card_type = 0;
    priv->tag = NULL;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_synergy_new:
 * @id: unique synergy identifier
 * @name: display name
 * @synergy_type: the type of synergy
 *
 * Creates a new synergy definition.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LrgSynergy *
lrg_synergy_new (const gchar    *id,
                 const gchar    *name,
                 LrgSynergyType  synergy_type)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_SYNERGY,
                         "id", id,
                         "name", name,
                         "synergy-type", synergy_type,
                         NULL);
}

/**
 * lrg_synergy_new_keyword:
 * @id: unique synergy identifier
 * @name: display name
 * @keyword: the keyword for synergy
 * @min_count: minimum cards with keyword for synergy
 *
 * Creates a keyword-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LrgSynergy *
lrg_synergy_new_keyword (const gchar    *id,
                         const gchar    *name,
                         LrgCardKeyword  keyword,
                         guint           min_count)
{
    LrgSynergy *self;
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (min_count >= 1, NULL);

    self = g_object_new (LRG_TYPE_SYNERGY,
                         "id", id,
                         "name", name,
                         "synergy-type", LRG_SYNERGY_TYPE_KEYWORD,
                         "min-count", min_count,
                         NULL);

    priv = lrg_synergy_get_instance_private (self);
    priv->keyword = keyword;

    return self;
}

/**
 * lrg_synergy_new_card_type:
 * @id: unique synergy identifier
 * @name: display name
 * @card_type: the card type for synergy
 * @min_count: minimum cards of type for synergy
 *
 * Creates a card-type-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LrgSynergy *
lrg_synergy_new_card_type (const gchar *id,
                           const gchar *name,
                           LrgCardType  card_type,
                           guint        min_count)
{
    LrgSynergy *self;
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (min_count >= 1, NULL);

    self = g_object_new (LRG_TYPE_SYNERGY,
                         "id", id,
                         "name", name,
                         "synergy-type", LRG_SYNERGY_TYPE_CARD_TYPE,
                         "min-count", min_count,
                         NULL);

    priv = lrg_synergy_get_instance_private (self);
    priv->card_type = card_type;

    return self;
}

/**
 * lrg_synergy_new_tag:
 * @id: unique synergy identifier
 * @name: display name
 * @tag: the tag for synergy
 * @min_count: minimum cards with tag for synergy
 *
 * Creates a tag-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LrgSynergy *
lrg_synergy_new_tag (const gchar *id,
                     const gchar *name,
                     const gchar *tag,
                     guint        min_count)
{
    LrgSynergy *self;
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (tag != NULL, NULL);
    g_return_val_if_fail (min_count >= 1, NULL);

    self = g_object_new (LRG_TYPE_SYNERGY,
                         "id", id,
                         "name", name,
                         "synergy-type", LRG_SYNERGY_TYPE_TAG,
                         "min-count", min_count,
                         NULL);

    priv = lrg_synergy_get_instance_private (self);
    priv->tag = g_strdup (tag);

    return self;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_synergy_get_id:
 * @self: a #LrgSynergy
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the synergy ID
 *
 * Since: 1.0
 */
const gchar *
lrg_synergy_get_id (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), NULL);

    priv = lrg_synergy_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_synergy_get_name:
 * @self: a #LrgSynergy
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the synergy name
 *
 * Since: 1.0
 */
const gchar *
lrg_synergy_get_name (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), NULL);

    priv = lrg_synergy_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_synergy_get_description:
 * @self: a #LrgSynergy
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_synergy_get_description (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), NULL);

    priv = lrg_synergy_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_synergy_set_description:
 * @self: a #LrgSynergy
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
void
lrg_synergy_set_description (LrgSynergy  *self,
                             const gchar *description)
{
    LrgSynergyPrivate *priv;

    g_return_if_fail (LRG_IS_SYNERGY (self));

    priv = lrg_synergy_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) == 0)
        return;

    g_clear_pointer (&priv->description, g_free);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

/**
 * lrg_synergy_get_synergy_type:
 * @self: a #LrgSynergy
 *
 * Gets the synergy type.
 *
 * Returns: the synergy type
 *
 * Since: 1.0
 */
LrgSynergyType
lrg_synergy_get_synergy_type (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), LRG_SYNERGY_TYPE_KEYWORD);

    priv = lrg_synergy_get_instance_private (self);
    return priv->synergy_type;
}

/**
 * lrg_synergy_get_min_count:
 * @self: a #LrgSynergy
 *
 * Gets the minimum card count for synergy activation.
 *
 * Returns: the minimum count
 *
 * Since: 1.0
 */
guint
lrg_synergy_get_min_count (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), 0);

    priv = lrg_synergy_get_instance_private (self);
    return priv->min_count;
}

/**
 * lrg_synergy_set_min_count:
 * @self: a #LrgSynergy
 * @min_count: the minimum count
 *
 * Sets the minimum card count for synergy activation.
 *
 * Since: 1.0
 */
void
lrg_synergy_set_min_count (LrgSynergy *self,
                           guint       min_count)
{
    LrgSynergyPrivate *priv;

    g_return_if_fail (LRG_IS_SYNERGY (self));
    g_return_if_fail (min_count >= 1);

    priv = lrg_synergy_get_instance_private (self);

    if (priv->min_count == min_count)
        return;

    priv->min_count = min_count;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_COUNT]);
}

/**
 * lrg_synergy_get_bonus_per_card:
 * @self: a #LrgSynergy
 *
 * Gets the bonus value per additional card.
 *
 * Returns: the bonus per card
 *
 * Since: 1.0
 */
gint
lrg_synergy_get_bonus_per_card (LrgSynergy *self)
{
    LrgSynergyPrivate *priv;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), 0);

    priv = lrg_synergy_get_instance_private (self);
    return priv->bonus_per_card;
}

/**
 * lrg_synergy_set_bonus_per_card:
 * @self: a #LrgSynergy
 * @bonus: the bonus per card
 *
 * Sets the bonus value per additional card.
 *
 * Since: 1.0
 */
void
lrg_synergy_set_bonus_per_card (LrgSynergy *self,
                                gint        bonus)
{
    LrgSynergyPrivate *priv;

    g_return_if_fail (LRG_IS_SYNERGY (self));

    priv = lrg_synergy_get_instance_private (self);

    if (priv->bonus_per_card == bonus)
        return;

    priv->bonus_per_card = bonus;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BONUS_PER_CARD]);
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_synergy_check_cards:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of cards to check
 *
 * Checks if the given cards have this synergy active.
 *
 * Returns: %TRUE if synergy is active
 *
 * Since: 1.0
 */
gboolean
lrg_synergy_check_cards (LrgSynergy *self,
                         GPtrArray  *cards)
{
    LrgSynergyClass *klass;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), FALSE);

    klass = LRG_SYNERGY_GET_CLASS (self);
    if (klass->check_cards != NULL)
        return klass->check_cards (self, cards);

    return FALSE;
}

/**
 * lrg_synergy_calculate_bonus:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of cards
 *
 * Calculates the bonus value from this synergy.
 *
 * Returns: the bonus value, or 0 if synergy not active
 *
 * Since: 1.0
 */
gint
lrg_synergy_calculate_bonus (LrgSynergy *self,
                             GPtrArray  *cards)
{
    LrgSynergyClass *klass;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), 0);

    klass = LRG_SYNERGY_GET_CLASS (self);
    if (klass->calculate_bonus != NULL)
        return klass->calculate_bonus (self, cards);

    return 0;
}

/**
 * lrg_synergy_get_synergy_cards:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of all cards
 *
 * Gets the subset of cards that contribute to this synergy.
 *
 * Returns: (transfer container) (element-type gpointer): contributing cards
 *
 * Since: 1.0
 */
GPtrArray *
lrg_synergy_get_synergy_cards (LrgSynergy *self,
                               GPtrArray  *cards)
{
    LrgSynergyClass *klass;

    g_return_val_if_fail (LRG_IS_SYNERGY (self), NULL);

    klass = LRG_SYNERGY_GET_CLASS (self);
    if (klass->get_synergy_cards != NULL)
        return klass->get_synergy_cards (self, cards);

    return g_ptr_array_new ();
}
