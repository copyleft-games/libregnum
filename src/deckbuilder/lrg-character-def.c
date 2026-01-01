/* lrg-character-def.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-character-def.h"
#include "../lrg-log.h"

/**
 * LrgCharacterDef:
 *
 * Base class for playable character definitions.
 *
 * Since: 1.0
 */

typedef struct
{
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *icon;

    /* Stats */
    gint base_hp;
    gint base_energy;
    gint base_draw;
    gint starting_gold;

    /* Starting deck: array of (card_id, count) pairs */
    GPtrArray *starting_deck_ids;
    GArray    *starting_deck_counts;
    gchar     *starting_relic_id;

    /* Unlock */
    gboolean unlocked_by_default;
    gchar   *unlock_requirement;
} LrgCharacterDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCharacterDef, lrg_character_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_BASE_HP,
    PROP_BASE_ENERGY,
    PROP_BASE_DRAW,
    PROP_STARTING_GOLD,
    PROP_STARTING_RELIC,
    PROP_UNLOCKED_BY_DEFAULT,
    PROP_UNLOCK_REQUIREMENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static GPtrArray *
lrg_character_def_real_get_starting_deck (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;
    GPtrArray              *deck;
    guint                   i;

    priv = lrg_character_def_get_instance_private (self);

    deck = g_ptr_array_new_with_free_func (g_free);

    for (i = 0; i < priv->starting_deck_ids->len; i++)
    {
        const gchar *card_id;
        gint         count;
        gint         j;

        card_id = g_ptr_array_index (priv->starting_deck_ids, i);
        count = g_array_index (priv->starting_deck_counts, gint, i);

        for (j = 0; j < count; j++)
        {
            g_ptr_array_add (deck, g_strdup (card_id));
        }
    }

    return deck;
}

static const gchar *
lrg_character_def_real_get_starting_relic (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    priv = lrg_character_def_get_instance_private (self);
    return priv->starting_relic_id;
}

static void
lrg_character_def_real_on_run_start (LrgCharacterDef *self,
                                      gpointer         run)
{
    /* Default: no-op */
}

static void
lrg_character_def_real_on_run_end (LrgCharacterDef *self,
                                    gpointer         run,
                                    gboolean         victory)
{
    /* Default: no-op */
}

static gint
lrg_character_def_real_modify_starting_hp (LrgCharacterDef *self,
                                            gint             base_hp)
{
    /* Default: no modification */
    return base_hp;
}

static gint
lrg_character_def_real_modify_starting_gold (LrgCharacterDef *self,
                                              gint             base_gold)
{
    /* Default: no modification */
    return base_gold;
}

static gboolean
lrg_character_def_real_can_unlock (LrgCharacterDef  *self,
                                    LrgPlayerProfile *profile)
{
    LrgCharacterDefPrivate *priv;

    priv = lrg_character_def_get_instance_private (self);

    /* Default: unlocked if marked as unlocked_by_default */
    return priv->unlocked_by_default;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_character_def_finalize (GObject *object)
{
    LrgCharacterDef        *self;
    LrgCharacterDefPrivate *priv;

    self = LRG_CHARACTER_DEF (object);
    priv = lrg_character_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->starting_relic_id, g_free);
    g_clear_pointer (&priv->unlock_requirement, g_free);
    g_clear_pointer (&priv->starting_deck_ids, g_ptr_array_unref);
    g_clear_pointer (&priv->starting_deck_counts, g_array_unref);

    G_OBJECT_CLASS (lrg_character_def_parent_class)->finalize (object);
}

static void
lrg_character_def_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgCharacterDef        *self;
    LrgCharacterDefPrivate *priv;

    self = LRG_CHARACTER_DEF (object);
    priv = lrg_character_def_get_instance_private (self);

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

        case PROP_ICON:
            g_value_set_string (value, priv->icon);
            break;

        case PROP_BASE_HP:
            g_value_set_int (value, priv->base_hp);
            break;

        case PROP_BASE_ENERGY:
            g_value_set_int (value, priv->base_energy);
            break;

        case PROP_BASE_DRAW:
            g_value_set_int (value, priv->base_draw);
            break;

        case PROP_STARTING_GOLD:
            g_value_set_int (value, priv->starting_gold);
            break;

        case PROP_STARTING_RELIC:
            g_value_set_string (value, priv->starting_relic_id);
            break;

        case PROP_UNLOCKED_BY_DEFAULT:
            g_value_set_boolean (value, priv->unlocked_by_default);
            break;

        case PROP_UNLOCK_REQUIREMENT:
            g_value_set_string (value, priv->unlock_requirement);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_character_def_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgCharacterDef        *self;
    LrgCharacterDefPrivate *priv;

    self = LRG_CHARACTER_DEF (object);
    priv = lrg_character_def_get_instance_private (self);

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

        case PROP_ICON:
            g_clear_pointer (&priv->icon, g_free);
            priv->icon = g_value_dup_string (value);
            break;

        case PROP_BASE_HP:
            priv->base_hp = g_value_get_int (value);
            break;

        case PROP_BASE_ENERGY:
            priv->base_energy = g_value_get_int (value);
            break;

        case PROP_BASE_DRAW:
            priv->base_draw = g_value_get_int (value);
            break;

        case PROP_STARTING_GOLD:
            priv->starting_gold = g_value_get_int (value);
            break;

        case PROP_STARTING_RELIC:
            g_clear_pointer (&priv->starting_relic_id, g_free);
            priv->starting_relic_id = g_value_dup_string (value);
            break;

        case PROP_UNLOCKED_BY_DEFAULT:
            priv->unlocked_by_default = g_value_get_boolean (value);
            break;

        case PROP_UNLOCK_REQUIREMENT:
            g_clear_pointer (&priv->unlock_requirement, g_free);
            priv->unlock_requirement = g_value_dup_string (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_character_def_class_init (LrgCharacterDefClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_character_def_finalize;
    object_class->get_property = lrg_character_def_get_property;
    object_class->set_property = lrg_character_def_set_property;

    /* Virtual methods */
    klass->get_starting_deck = lrg_character_def_real_get_starting_deck;
    klass->get_starting_relic = lrg_character_def_real_get_starting_relic;
    klass->on_run_start = lrg_character_def_real_on_run_start;
    klass->on_run_end = lrg_character_def_real_on_run_end;
    klass->modify_starting_hp = lrg_character_def_real_modify_starting_hp;
    klass->modify_starting_gold = lrg_character_def_real_modify_starting_gold;
    klass->can_unlock = lrg_character_def_real_can_unlock;

    /**
     * LrgCharacterDef:id:
     *
     * The character's unique identifier.
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
     * LrgCharacterDef:name:
     *
     * The character's display name.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:description:
     *
     * The character's description.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Character description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:icon:
     *
     * Path to the character's icon/portrait.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon/portrait path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:base-hp:
     *
     * The character's base maximum HP.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_HP] =
        g_param_spec_int ("base-hp",
                          "Base HP",
                          "Base maximum HP",
                          1, G_MAXINT, 80,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:base-energy:
     *
     * The character's base energy per turn.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_ENERGY] =
        g_param_spec_int ("base-energy",
                          "Base Energy",
                          "Base energy per turn",
                          0, G_MAXINT, 3,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:base-draw:
     *
     * The character's base cards drawn per turn.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_DRAW] =
        g_param_spec_int ("base-draw",
                          "Base Draw",
                          "Base cards drawn per turn",
                          0, G_MAXINT, 5,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:starting-gold:
     *
     * The character's starting gold.
     *
     * Since: 1.0
     */
    properties[PROP_STARTING_GOLD] =
        g_param_spec_int ("starting-gold",
                          "Starting Gold",
                          "Starting gold amount",
                          0, G_MAXINT, 99,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:starting-relic:
     *
     * The ID of the character's starting relic.
     *
     * Since: 1.0
     */
    properties[PROP_STARTING_RELIC] =
        g_param_spec_string ("starting-relic",
                             "Starting Relic",
                             "Starting relic ID",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:unlocked-by-default:
     *
     * Whether this character is unlocked by default.
     *
     * Since: 1.0
     */
    properties[PROP_UNLOCKED_BY_DEFAULT] =
        g_param_spec_boolean ("unlocked-by-default",
                              "Unlocked by Default",
                              "Whether unlocked by default",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCharacterDef:unlock-requirement:
     *
     * Description of how to unlock this character.
     *
     * Since: 1.0
     */
    properties[PROP_UNLOCK_REQUIREMENT] =
        g_param_spec_string ("unlock-requirement",
                             "Unlock Requirement",
                             "Unlock requirement description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_character_def_init (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    priv = lrg_character_def_get_instance_private (self);

    priv->starting_deck_ids = g_ptr_array_new_with_free_func (g_free);
    priv->starting_deck_counts = g_array_new (FALSE, TRUE, sizeof (gint));

    /* Defaults similar to Slay the Spire */
    priv->base_hp = 80;
    priv->base_energy = 3;
    priv->base_draw = 5;
    priv->starting_gold = 99;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_character_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new character definition.
 *
 * Returns: (transfer full): a new #LrgCharacterDef
 *
 * Since: 1.0
 */
LrgCharacterDef *
lrg_character_def_new (const gchar *id,
                        const gchar *name)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_CHARACTER_DEF,
                         "id", id,
                         "name", name,
                         NULL);
}

/**
 * lrg_character_def_get_id:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_id (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    priv = lrg_character_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_character_def_get_name:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_name (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    priv = lrg_character_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_character_def_set_name:
 * @self: a #LrgCharacterDef
 * @name: the new name
 *
 * Sets the character's display name.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_name (LrgCharacterDef *self,
                             const gchar     *name)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "name", name, NULL);
}

/**
 * lrg_character_def_get_description:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_description (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    priv = lrg_character_def_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_character_def_set_description:
 * @self: a #LrgCharacterDef
 * @description: (nullable): the description
 *
 * Sets the character's description.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_description (LrgCharacterDef *self,
                                    const gchar     *description)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "description", description, NULL);
}

/**
 * lrg_character_def_get_icon:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's icon/portrait path.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_icon (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    priv = lrg_character_def_get_instance_private (self);
    return priv->icon;
}

/**
 * lrg_character_def_set_icon:
 * @self: a #LrgCharacterDef
 * @icon: (nullable): the icon path
 *
 * Sets the character's icon/portrait path.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_icon (LrgCharacterDef *self,
                             const gchar     *icon)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "icon", icon, NULL);
}

/**
 * lrg_character_def_get_base_hp:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base maximum HP.
 *
 * Returns: base HP
 *
 * Since: 1.0
 */
gint
lrg_character_def_get_base_hp (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), 0);

    priv = lrg_character_def_get_instance_private (self);
    return priv->base_hp;
}

/**
 * lrg_character_def_set_base_hp:
 * @self: a #LrgCharacterDef
 * @base_hp: base HP
 *
 * Sets the character's base maximum HP.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_base_hp (LrgCharacterDef *self,
                                gint             base_hp)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "base-hp", base_hp, NULL);
}

/**
 * lrg_character_def_get_base_energy:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base energy per turn.
 *
 * Returns: base energy
 *
 * Since: 1.0
 */
gint
lrg_character_def_get_base_energy (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), 0);

    priv = lrg_character_def_get_instance_private (self);
    return priv->base_energy;
}

/**
 * lrg_character_def_set_base_energy:
 * @self: a #LrgCharacterDef
 * @base_energy: base energy per turn
 *
 * Sets the character's base energy per turn.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_base_energy (LrgCharacterDef *self,
                                    gint             base_energy)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "base-energy", base_energy, NULL);
}

/**
 * lrg_character_def_get_base_draw:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base cards drawn per turn.
 *
 * Returns: base draw
 *
 * Since: 1.0
 */
gint
lrg_character_def_get_base_draw (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), 0);

    priv = lrg_character_def_get_instance_private (self);
    return priv->base_draw;
}

/**
 * lrg_character_def_set_base_draw:
 * @self: a #LrgCharacterDef
 * @base_draw: base cards drawn per turn
 *
 * Sets the character's base cards drawn per turn.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_base_draw (LrgCharacterDef *self,
                                  gint             base_draw)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "base-draw", base_draw, NULL);
}

/**
 * lrg_character_def_get_starting_gold:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's starting gold.
 *
 * Returns: starting gold
 *
 * Since: 1.0
 */
gint
lrg_character_def_get_starting_gold (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), 0);

    priv = lrg_character_def_get_instance_private (self);
    return priv->starting_gold;
}

/**
 * lrg_character_def_set_starting_gold:
 * @self: a #LrgCharacterDef
 * @starting_gold: starting gold
 *
 * Sets the character's starting gold.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_starting_gold (LrgCharacterDef *self,
                                      gint             starting_gold)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "starting-gold", starting_gold, NULL);
}

/**
 * lrg_character_def_add_starting_card:
 * @self: a #LrgCharacterDef
 * @card_id: the card ID to add
 * @count: number of copies to add
 *
 * Adds cards to the starting deck.
 *
 * Since: 1.0
 */
void
lrg_character_def_add_starting_card (LrgCharacterDef *self,
                                      const gchar     *card_id,
                                      gint             count)
{
    LrgCharacterDefPrivate *priv;

    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));
    g_return_if_fail (card_id != NULL);
    g_return_if_fail (count > 0);

    priv = lrg_character_def_get_instance_private (self);

    g_ptr_array_add (priv->starting_deck_ids, g_strdup (card_id));
    g_array_append_val (priv->starting_deck_counts, count);
}

/**
 * lrg_character_def_get_starting_deck:
 * @self: a #LrgCharacterDef
 *
 * Gets the starting deck card IDs.
 *
 * Returns: (transfer full) (element-type utf8): array of card IDs
 *
 * Since: 1.0
 */
GPtrArray *
lrg_character_def_get_starting_deck (LrgCharacterDef *self)
{
    LrgCharacterDefClass *klass;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->get_starting_deck != NULL)
        return klass->get_starting_deck (self);

    return g_ptr_array_new ();
}

/**
 * lrg_character_def_set_starting_relic:
 * @self: a #LrgCharacterDef
 * @relic_id: (nullable): the starting relic ID
 *
 * Sets the character's starting relic.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_starting_relic (LrgCharacterDef *self,
                                       const gchar     *relic_id)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "starting-relic", relic_id, NULL);
}

/**
 * lrg_character_def_get_starting_relic:
 * @self: a #LrgCharacterDef
 *
 * Gets the starting relic ID.
 *
 * Returns: (transfer none) (nullable): the relic ID
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_starting_relic (LrgCharacterDef *self)
{
    LrgCharacterDefClass *klass;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->get_starting_relic != NULL)
        return klass->get_starting_relic (self);

    return NULL;
}

/**
 * lrg_character_def_get_unlocked_by_default:
 * @self: a #LrgCharacterDef
 *
 * Gets whether this character is unlocked by default.
 *
 * Returns: %TRUE if unlocked by default
 *
 * Since: 1.0
 */
gboolean
lrg_character_def_get_unlocked_by_default (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), FALSE);

    priv = lrg_character_def_get_instance_private (self);
    return priv->unlocked_by_default;
}

/**
 * lrg_character_def_set_unlocked_by_default:
 * @self: a #LrgCharacterDef
 * @unlocked: whether unlocked by default
 *
 * Sets whether this character is unlocked by default.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_unlocked_by_default (LrgCharacterDef *self,
                                            gboolean         unlocked)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "unlocked-by-default", unlocked, NULL);
}

/**
 * lrg_character_def_get_unlock_requirement:
 * @self: a #LrgCharacterDef
 *
 * Gets the unlock requirement description.
 *
 * Returns: (transfer none) (nullable): requirement text
 *
 * Since: 1.0
 */
const gchar *
lrg_character_def_get_unlock_requirement (LrgCharacterDef *self)
{
    LrgCharacterDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), NULL);

    priv = lrg_character_def_get_instance_private (self);
    return priv->unlock_requirement;
}

/**
 * lrg_character_def_set_unlock_requirement:
 * @self: a #LrgCharacterDef
 * @requirement: (nullable): requirement description
 *
 * Sets the unlock requirement description.
 *
 * Since: 1.0
 */
void
lrg_character_def_set_unlock_requirement (LrgCharacterDef *self,
                                           const gchar     *requirement)
{
    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    g_object_set (self, "unlock-requirement", requirement, NULL);
}

/**
 * lrg_character_def_on_run_start:
 * @self: a #LrgCharacterDef
 * @run: (nullable): the run context
 *
 * Called when a run starts with this character.
 *
 * Since: 1.0
 */
void
lrg_character_def_on_run_start (LrgCharacterDef *self,
                                 gpointer         run)
{
    LrgCharacterDefClass *klass;

    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->on_run_start != NULL)
        klass->on_run_start (self, run);
}

/**
 * lrg_character_def_on_run_end:
 * @self: a #LrgCharacterDef
 * @run: (nullable): the run context
 * @victory: whether the run was won
 *
 * Called when a run ends.
 *
 * Since: 1.0
 */
void
lrg_character_def_on_run_end (LrgCharacterDef *self,
                               gpointer         run,
                               gboolean         victory)
{
    LrgCharacterDefClass *klass;

    g_return_if_fail (LRG_IS_CHARACTER_DEF (self));

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->on_run_end != NULL)
        klass->on_run_end (self, run, victory);
}

/**
 * lrg_character_def_modify_starting_hp:
 * @self: a #LrgCharacterDef
 * @base_hp: base HP value
 *
 * Modifies starting HP (for ascension effects).
 *
 * Returns: modified HP
 *
 * Since: 1.0
 */
gint
lrg_character_def_modify_starting_hp (LrgCharacterDef *self,
                                       gint             base_hp)
{
    LrgCharacterDefClass *klass;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), base_hp);

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->modify_starting_hp != NULL)
        return klass->modify_starting_hp (self, base_hp);

    return base_hp;
}

/**
 * lrg_character_def_modify_starting_gold:
 * @self: a #LrgCharacterDef
 * @base_gold: base gold value
 *
 * Modifies starting gold.
 *
 * Returns: modified gold
 *
 * Since: 1.0
 */
gint
lrg_character_def_modify_starting_gold (LrgCharacterDef *self,
                                         gint             base_gold)
{
    LrgCharacterDefClass *klass;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), base_gold);

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->modify_starting_gold != NULL)
        return klass->modify_starting_gold (self, base_gold);

    return base_gold;
}

/**
 * lrg_character_def_can_unlock:
 * @self: a #LrgCharacterDef
 * @profile: (nullable): player profile to check against
 *
 * Checks if this character can be unlocked.
 *
 * Returns: %TRUE if unlock conditions are met
 *
 * Since: 1.0
 */
gboolean
lrg_character_def_can_unlock (LrgCharacterDef  *self,
                               LrgPlayerProfile *profile)
{
    LrgCharacterDefClass *klass;

    g_return_val_if_fail (LRG_IS_CHARACTER_DEF (self), FALSE);

    klass = LRG_CHARACTER_DEF_GET_CLASS (self);

    if (klass->can_unlock != NULL)
        return klass->can_unlock (self, profile);

    return FALSE;
}
