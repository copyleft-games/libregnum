/* lrg-deck-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-deck-def.h"
#include "lrg-card-def.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgDeckDef:
 *
 * Template definition for a deck.
 *
 * Defines the starting cards and constraints for deck construction.
 * This is a derivable class - subclass it to create custom deck types
 * with special validation or dynamic starting cards.
 */

typedef struct
{
    gchar     *id;
    gchar     *name;
    gchar     *description;
    gchar     *character_id;
    guint      min_size;
    guint      max_size;
    GPtrArray *starting_cards;   /* Array of LrgDeckCardEntry */
    GArray    *allowed_types;    /* Array of LrgCardType */
    GPtrArray *banned_cards;     /* Array of LrgCardDef */
} LrgDeckDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgDeckDef, lrg_deck_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_CHARACTER_ID,
    PROP_MIN_SIZE,
    PROP_MAX_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * LrgDeckCardEntry GBoxed
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgDeckCardEntry,
                     lrg_deck_card_entry,
                     lrg_deck_card_entry_copy,
                     lrg_deck_card_entry_free)

LrgDeckCardEntry *
lrg_deck_card_entry_new (LrgCardDef *card_def,
                         guint       count)
{
    LrgDeckCardEntry *entry;

    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), NULL);
    g_return_val_if_fail (count > 0, NULL);

    entry = g_slice_new0 (LrgDeckCardEntry);
    entry->card_def = g_object_ref (card_def);
    entry->count = count;

    return entry;
}

LrgDeckCardEntry *
lrg_deck_card_entry_copy (LrgDeckCardEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);

    return lrg_deck_card_entry_new (entry->card_def, entry->count);
}

void
lrg_deck_card_entry_free (LrgDeckCardEntry *entry)
{
    if (entry == NULL)
        return;

    g_clear_object (&entry->card_def);
    g_slice_free (LrgDeckCardEntry, entry);
}

/* ==========================================================================
 * Virtual method defaults
 * ========================================================================== */

static gboolean
lrg_deck_def_real_validate (LrgDeckDef  *self,
                            GError     **error)
{
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);
    guint total_cards;

    total_cards = lrg_deck_def_get_total_starting_cards (self);

    /* Check minimum size */
    if (priv->min_size > 0 && total_cards < priv->min_size)
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_DECK_TOO_SMALL,
                     "Deck has %u cards, minimum is %u",
                     total_cards, priv->min_size);
        return FALSE;
    }

    /* Check maximum size */
    if (priv->max_size > 0 && total_cards > priv->max_size)
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE,
                     "Deck has %u cards, maximum is %u",
                     total_cards, priv->max_size);
        return FALSE;
    }

    return TRUE;
}

static GPtrArray *
lrg_deck_def_real_get_starting_cards (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);

    return priv->starting_cards;
}

/* ==========================================================================
 * GObject implementation
 * ========================================================================== */

static void
lrg_deck_def_finalize (GObject *object)
{
    LrgDeckDef *self = LRG_DECK_DEF (object);
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->character_id, g_free);
    g_clear_pointer (&priv->starting_cards, g_ptr_array_unref);
    g_clear_pointer (&priv->allowed_types, g_array_unref);
    g_clear_pointer (&priv->banned_cards, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_deck_def_parent_class)->finalize (object);
}

static void
lrg_deck_def_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgDeckDef *self = LRG_DECK_DEF (object);
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);

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
    case PROP_CHARACTER_ID:
        g_value_set_string (value, priv->character_id);
        break;
    case PROP_MIN_SIZE:
        g_value_set_uint (value, priv->min_size);
        break;
    case PROP_MAX_SIZE:
        g_value_set_uint (value, priv->max_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_def_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgDeckDef *self = LRG_DECK_DEF (object);
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_CHARACTER_ID:
        g_free (priv->character_id);
        priv->character_id = g_value_dup_string (value);
        break;
    case PROP_MIN_SIZE:
        priv->min_size = g_value_get_uint (value);
        break;
    case PROP_MAX_SIZE:
        priv->max_size = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_deck_def_class_init (LrgDeckDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_deck_def_finalize;
    object_class->get_property = lrg_deck_def_get_property;
    object_class->set_property = lrg_deck_def_set_property;

    /* Virtual methods */
    klass->validate = lrg_deck_def_real_validate;
    klass->get_starting_cards = lrg_deck_def_real_get_starting_cards;

    /**
     * LrgDeckDef:id:
     *
     * The unique identifier for this deck definition.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "The unique identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckDef:name:
     *
     * The display name for this deck.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "The display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckDef:description:
     *
     * Description of the deck and its playstyle.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Description of the deck",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckDef:character-id:
     *
     * ID of the character this deck belongs to.
     *
     * Since: 1.0
     */
    properties[PROP_CHARACTER_ID] =
        g_param_spec_string ("character-id",
                             "Character ID",
                             "ID of the associated character",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckDef:min-size:
     *
     * Minimum number of cards required in the deck.
     * Set to 0 for no minimum.
     *
     * Since: 1.0
     */
    properties[PROP_MIN_SIZE] =
        g_param_spec_uint ("min-size",
                           "Minimum Size",
                           "Minimum number of cards",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgDeckDef:max-size:
     *
     * Maximum number of cards allowed in the deck.
     * Set to 0 for no maximum.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_SIZE] =
        g_param_spec_uint ("max-size",
                           "Maximum Size",
                           "Maximum number of cards",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_deck_def_init (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv = lrg_deck_def_get_instance_private (self);

    priv->starting_cards = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_deck_card_entry_free);
    priv->allowed_types = g_array_new (FALSE, FALSE, sizeof (LrgCardType));
    priv->banned_cards = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_deck_def_new:
 * @id: unique identifier for this deck
 *
 * Creates a new deck definition.
 *
 * Returns: (transfer full): a new #LrgDeckDef
 *
 * Since: 1.0
 */
LrgDeckDef *
lrg_deck_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_DECK_DEF,
                         "id", id,
                         NULL);
}

/**
 * lrg_deck_def_get_id:
 * @self: a #LrgDeckDef
 *
 * Gets the deck's unique identifier.
 *
 * Returns: (transfer none): the identifier
 *
 * Since: 1.0
 */
const gchar *
lrg_deck_def_get_id (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), NULL);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_deck_def_get_name:
 * @self: a #LrgDeckDef
 *
 * Gets the deck's display name.
 *
 * Returns: (transfer none) (nullable): the name
 *
 * Since: 1.0
 */
const gchar *
lrg_deck_def_get_name (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), NULL);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_deck_def_set_name:
 * @self: a #LrgDeckDef
 * @name: the display name
 *
 * Sets the deck's display name.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_name (LrgDeckDef  *self,
                       const gchar *name)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/**
 * lrg_deck_def_get_description:
 * @self: a #LrgDeckDef
 *
 * Gets the deck description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_deck_def_get_description (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), NULL);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_deck_def_set_description:
 * @self: a #LrgDeckDef
 * @description: the description
 *
 * Sets the deck description.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_description (LrgDeckDef  *self,
                              const gchar *description)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) != 0)
    {
        g_free (priv->description);
        priv->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

/**
 * lrg_deck_def_get_character_id:
 * @self: a #LrgDeckDef
 *
 * Gets the ID of the character this deck belongs to.
 *
 * Returns: (transfer none) (nullable): the character ID
 *
 * Since: 1.0
 */
const gchar *
lrg_deck_def_get_character_id (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), NULL);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->character_id;
}

/**
 * lrg_deck_def_set_character_id:
 * @self: a #LrgDeckDef
 * @character_id: (nullable): the character ID
 *
 * Sets the ID of the character this deck belongs to.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_character_id (LrgDeckDef  *self,
                               const gchar *character_id)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    if (g_strcmp0 (priv->character_id, character_id) != 0)
    {
        g_free (priv->character_id);
        priv->character_id = g_strdup (character_id);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHARACTER_ID]);
    }
}

/**
 * lrg_deck_def_get_min_size:
 * @self: a #LrgDeckDef
 *
 * Gets the minimum deck size.
 *
 * Returns: the minimum size, or 0 for no minimum
 *
 * Since: 1.0
 */
guint
lrg_deck_def_get_min_size (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), 0);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->min_size;
}

/**
 * lrg_deck_def_set_min_size:
 * @self: a #LrgDeckDef
 * @min_size: the minimum size
 *
 * Sets the minimum deck size.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_min_size (LrgDeckDef *self,
                           guint       min_size)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    if (priv->min_size != min_size)
    {
        priv->min_size = min_size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_SIZE]);
    }
}

/**
 * lrg_deck_def_get_max_size:
 * @self: a #LrgDeckDef
 *
 * Gets the maximum deck size.
 *
 * Returns: the maximum size, or 0 for no maximum
 *
 * Since: 1.0
 */
guint
lrg_deck_def_get_max_size (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), 0);

    priv = lrg_deck_def_get_instance_private (self);
    return priv->max_size;
}

/**
 * lrg_deck_def_set_max_size:
 * @self: a #LrgDeckDef
 * @max_size: the maximum size
 *
 * Sets the maximum deck size.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_max_size (LrgDeckDef *self,
                           guint       max_size)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    if (priv->max_size != max_size)
    {
        priv->max_size = max_size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_SIZE]);
    }
}

/**
 * lrg_deck_def_add_starting_card:
 * @self: a #LrgDeckDef
 * @card_def: the card definition
 * @count: number of copies
 *
 * Adds cards to the starting deck.
 *
 * Since: 1.0
 */
void
lrg_deck_def_add_starting_card (LrgDeckDef *self,
                                LrgCardDef *card_def,
                                guint       count)
{
    LrgDeckDefPrivate *priv;
    LrgDeckCardEntry *entry;
    guint i;

    g_return_if_fail (LRG_IS_DECK_DEF (self));
    g_return_if_fail (LRG_IS_CARD_DEF (card_def));
    g_return_if_fail (count > 0);

    priv = lrg_deck_def_get_instance_private (self);

    /* Check if already exists, update count */
    for (i = 0; i < priv->starting_cards->len; i++)
    {
        entry = g_ptr_array_index (priv->starting_cards, i);
        if (entry->card_def == card_def)
        {
            entry->count += count;
            return;
        }
    }

    /* Add new entry */
    entry = lrg_deck_card_entry_new (card_def, count);
    g_ptr_array_add (priv->starting_cards, entry);

    lrg_log_debug ("Added %u x '%s' to deck '%s'",
                   count,
                   lrg_card_def_get_id (card_def),
                   priv->id);
}

/**
 * lrg_deck_def_remove_starting_card:
 * @self: a #LrgDeckDef
 * @card_def: the card definition to remove
 *
 * Removes all copies of a card from the starting deck.
 *
 * Returns: %TRUE if the card was found and removed, %FALSE otherwise
 *
 * Since: 1.0
 */
gboolean
lrg_deck_def_remove_starting_card (LrgDeckDef *self,
                                   LrgCardDef *card_def)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), FALSE);

    priv = lrg_deck_def_get_instance_private (self);

    for (i = 0; i < priv->starting_cards->len; i++)
    {
        LrgDeckCardEntry *entry = g_ptr_array_index (priv->starting_cards, i);
        if (entry->card_def == card_def)
        {
            g_ptr_array_remove_index (priv->starting_cards, i);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_deck_def_clear_starting_cards:
 * @self: a #LrgDeckDef
 *
 * Removes all starting cards from the deck.
 *
 * Since: 1.0
 */
void
lrg_deck_def_clear_starting_cards (LrgDeckDef *self)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);
    g_ptr_array_set_size (priv->starting_cards, 0);
}

/**
 * lrg_deck_def_get_starting_cards:
 * @self: a #LrgDeckDef
 *
 * Gets the starting cards for this deck.
 * Calls the virtual method, which can be overridden
 * for dynamic starting card generation.
 *
 * Returns: (element-type LrgDeckCardEntry) (transfer none): starting cards
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deck_def_get_starting_cards (LrgDeckDef *self)
{
    LrgDeckDefClass *klass;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), NULL);

    klass = LRG_DECK_DEF_GET_CLASS (self);
    if (klass->get_starting_cards != NULL)
        return klass->get_starting_cards (self);

    return NULL;
}

/**
 * lrg_deck_def_get_starting_card_count:
 * @self: a #LrgDeckDef
 *
 * Gets the number of distinct card entries in the starting deck.
 *
 * Returns: number of distinct card entries
 *
 * Since: 1.0
 */
guint
lrg_deck_def_get_starting_card_count (LrgDeckDef *self)
{
    GPtrArray *cards;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), 0);

    cards = lrg_deck_def_get_starting_cards (self);
    if (cards == NULL)
        return 0;

    return cards->len;
}

/**
 * lrg_deck_def_get_total_starting_cards:
 * @self: a #LrgDeckDef
 *
 * Gets the total number of cards in the starting deck (sum of all counts).
 *
 * Returns: total card count
 *
 * Since: 1.0
 */
guint
lrg_deck_def_get_total_starting_cards (LrgDeckDef *self)
{
    GPtrArray *cards;
    guint total;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), 0);

    cards = lrg_deck_def_get_starting_cards (self);
    if (cards == NULL)
        return 0;

    total = 0;
    for (i = 0; i < cards->len; i++)
    {
        LrgDeckCardEntry *entry = g_ptr_array_index (cards, i);
        total += entry->count;
    }

    return total;
}

/**
 * lrg_deck_def_add_allowed_card_type:
 * @self: a #LrgDeckDef
 * @card_type: the card type to allow
 *
 * Adds a card type to the allowed list.
 * If no types are added, all types are allowed.
 *
 * Since: 1.0
 */
void
lrg_deck_def_add_allowed_card_type (LrgDeckDef  *self,
                                    LrgCardType  card_type)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    /* Check if already exists */
    for (i = 0; i < priv->allowed_types->len; i++)
    {
        if (g_array_index (priv->allowed_types, LrgCardType, i) == card_type)
            return;
    }

    g_array_append_val (priv->allowed_types, card_type);
}

/**
 * lrg_deck_def_remove_allowed_card_type:
 * @self: a #LrgDeckDef
 * @card_type: the card type to remove
 *
 * Removes a card type from the allowed list.
 *
 * Since: 1.0
 */
void
lrg_deck_def_remove_allowed_card_type (LrgDeckDef  *self,
                                       LrgCardType  card_type)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    for (i = 0; i < priv->allowed_types->len; i++)
    {
        if (g_array_index (priv->allowed_types, LrgCardType, i) == card_type)
        {
            g_array_remove_index (priv->allowed_types, i);
            return;
        }
    }
}

/**
 * lrg_deck_def_is_card_type_allowed:
 * @self: a #LrgDeckDef
 * @card_type: the card type to check
 *
 * Checks if a card type is allowed in this deck.
 * If no types are explicitly allowed, all types are allowed.
 *
 * Returns: %TRUE if allowed
 *
 * Since: 1.0
 */
gboolean
lrg_deck_def_is_card_type_allowed (LrgDeckDef  *self,
                                   LrgCardType  card_type)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), FALSE);

    priv = lrg_deck_def_get_instance_private (self);

    /* If no restrictions, all types allowed */
    if (priv->allowed_types->len == 0)
        return TRUE;

    for (i = 0; i < priv->allowed_types->len; i++)
    {
        if (g_array_index (priv->allowed_types, LrgCardType, i) == card_type)
            return TRUE;
    }

    return FALSE;
}

/**
 * lrg_deck_def_add_banned_card:
 * @self: a #LrgDeckDef
 * @card_def: the card to ban
 *
 * Adds a card to the banned list.
 *
 * Since: 1.0
 */
void
lrg_deck_def_add_banned_card (LrgDeckDef *self,
                              LrgCardDef *card_def)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_DECK_DEF (self));
    g_return_if_fail (LRG_IS_CARD_DEF (card_def));

    priv = lrg_deck_def_get_instance_private (self);

    /* Check if already banned */
    for (i = 0; i < priv->banned_cards->len; i++)
    {
        if (g_ptr_array_index (priv->banned_cards, i) == card_def)
            return;
    }

    g_ptr_array_add (priv->banned_cards, g_object_ref (card_def));
}

/**
 * lrg_deck_def_remove_banned_card:
 * @self: a #LrgDeckDef
 * @card_def: the card to unban
 *
 * Removes a card from the banned list.
 *
 * Since: 1.0
 */
void
lrg_deck_def_remove_banned_card (LrgDeckDef *self,
                                 LrgCardDef *card_def)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));
    g_return_if_fail (LRG_IS_CARD_DEF (card_def));

    priv = lrg_deck_def_get_instance_private (self);
    g_ptr_array_remove (priv->banned_cards, card_def);
}

/**
 * lrg_deck_def_is_card_banned:
 * @self: a #LrgDeckDef
 * @card_def: the card to check
 *
 * Checks if a card is banned from this deck.
 *
 * Returns: %TRUE if banned
 *
 * Since: 1.0
 */
gboolean
lrg_deck_def_is_card_banned (LrgDeckDef *self,
                             LrgCardDef *card_def)
{
    LrgDeckDefPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), FALSE);

    priv = lrg_deck_def_get_instance_private (self);

    for (i = 0; i < priv->banned_cards->len; i++)
    {
        if (g_ptr_array_index (priv->banned_cards, i) == card_def)
            return TRUE;
    }

    return FALSE;
}

/**
 * lrg_deck_def_validate:
 * @self: a #LrgDeckDef
 * @error: (nullable): return location for error
 *
 * Validates the deck definition.
 * Calls the virtual method for custom validation.
 *
 * Returns: %TRUE if valid
 *
 * Since: 1.0
 */
gboolean
lrg_deck_def_validate (LrgDeckDef  *self,
                       GError     **error)
{
    LrgDeckDefClass *klass;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), FALSE);

    klass = LRG_DECK_DEF_GET_CLASS (self);
    if (klass->validate != NULL)
        return klass->validate (self, error);

    return TRUE;
}

/**
 * lrg_deck_def_set_allowed_types:
 * @self: a #LrgDeckDef
 * @card_type: the only card type to allow
 *
 * Clears all allowed types and sets only the specified type.
 * After calling this, only cards of this type can be added.
 *
 * Since: 1.0
 */
void
lrg_deck_def_set_allowed_types (LrgDeckDef  *self,
                                LrgCardType  card_type)
{
    LrgDeckDefPrivate *priv;

    g_return_if_fail (LRG_IS_DECK_DEF (self));

    priv = lrg_deck_def_get_instance_private (self);

    /* Clear existing and add the single type */
    g_array_set_size (priv->allowed_types, 0);
    g_array_append_val (priv->allowed_types, card_type);
}

/**
 * lrg_deck_def_add_allowed_type:
 * @self: a #LrgDeckDef
 * @card_type: the card type to allow
 *
 * Adds a card type to the allowed list.
 * Convenience wrapper for lrg_deck_def_add_allowed_card_type().
 *
 * Since: 1.0
 */
void
lrg_deck_def_add_allowed_type (LrgDeckDef  *self,
                               LrgCardType  card_type)
{
    lrg_deck_def_add_allowed_card_type (self, card_type);
}

/**
 * lrg_deck_def_ban_card:
 * @self: a #LrgDeckDef
 * @card_def: the card to ban
 *
 * Bans a card from this deck.
 * Convenience wrapper for lrg_deck_def_add_banned_card().
 *
 * Since: 1.0
 */
void
lrg_deck_def_ban_card (LrgDeckDef *self,
                       LrgCardDef *card_def)
{
    lrg_deck_def_add_banned_card (self, card_def);
}

/**
 * lrg_deck_def_unban_card:
 * @self: a #LrgDeckDef
 * @card_def: the card to unban
 *
 * Unbans a card from this deck.
 * Convenience wrapper for lrg_deck_def_remove_banned_card().
 *
 * Since: 1.0
 */
void
lrg_deck_def_unban_card (LrgDeckDef *self,
                         LrgCardDef *card_def)
{
    lrg_deck_def_remove_banned_card (self, card_def);
}

/**
 * lrg_deck_def_can_add_card:
 * @self: a #LrgDeckDef
 * @card_def: the card to check
 *
 * Checks if a card can be added to this deck based on
 * type restrictions and ban list.
 *
 * Returns: %TRUE if the card can be added
 *
 * Since: 1.0
 */
gboolean
lrg_deck_def_can_add_card (LrgDeckDef *self,
                           LrgCardDef *card_def)
{
    LrgCardType card_type;

    g_return_val_if_fail (LRG_IS_DECK_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_DEF (card_def), FALSE);

    /* Check if banned */
    if (lrg_deck_def_is_card_banned (self, card_def))
        return FALSE;

    /* Check if type is allowed */
    card_type = lrg_card_def_get_card_type (card_def);
    if (!lrg_deck_def_is_card_type_allowed (self, card_type))
        return FALSE;

    return TRUE;
}

/* ==========================================================================
 * LrgDeckCardEntry getters
 * ========================================================================== */

/**
 * lrg_deck_card_entry_get_card_def:
 * @entry: a #LrgDeckCardEntry
 *
 * Gets the card definition from an entry.
 *
 * Returns: (transfer none): the card definition
 *
 * Since: 1.0
 */
LrgCardDef *
lrg_deck_card_entry_get_card_def (LrgDeckCardEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);

    return entry->card_def;
}

/**
 * lrg_deck_card_entry_get_count:
 * @entry: a #LrgDeckCardEntry
 *
 * Gets the count from an entry.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
guint
lrg_deck_card_entry_get_count (LrgDeckCardEntry *entry)
{
    g_return_val_if_fail (entry != NULL, 0);

    return entry->count;
}

/**
 * lrg_deck_card_entry_set_count:
 * @entry: a #LrgDeckCardEntry
 * @count: the new count
 *
 * Sets the count on an entry.
 *
 * Since: 1.0
 */
void
lrg_deck_card_entry_set_count (LrgDeckCardEntry *entry,
                               guint             count)
{
    g_return_if_fail (entry != NULL);

    entry->count = count;
}
