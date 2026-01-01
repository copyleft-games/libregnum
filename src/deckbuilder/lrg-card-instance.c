/* lrg-card-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-card-instance.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/* Static counter for unique instance IDs */
static guint64 next_instance_id = 1;

/**
 * LrgCardInstance:
 *
 * Runtime instance of a card.
 */
struct _LrgCardInstance
{
    GObject parent_instance;

    LrgCardDef         *def;
    LrgCardUpgradeTier  upgrade_tier;
    LrgCardZone         zone;
    guint64             instance_id;

    /* Temporary combat modifiers */
    gint                cost_modifier;
    LrgCardKeyword      temporary_keywords;
    guint               times_played;

    /* Scoring modifiers (Balatro-style) */
    gint                bonus_chips;
};

G_DEFINE_TYPE (LrgCardInstance, lrg_card_instance, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_DEF,
    PROP_UPGRADE_TIER,
    PROP_ZONE,
    PROP_COST_MODIFIER,
    PROP_TIMES_PLAYED,
    PROP_BONUS_CHIPS,
    PROP_INSTANCE_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_ZONE_CHANGED,
    SIGNAL_UPGRADED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_card_instance_dispose (GObject *object)
{
    LrgCardInstance *self = LRG_CARD_INSTANCE (object);

    g_clear_object (&self->def);

    G_OBJECT_CLASS (lrg_card_instance_parent_class)->dispose (object);
}

static void
lrg_card_instance_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgCardInstance *self = LRG_CARD_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_value_set_object (value, self->def);
        break;
    case PROP_UPGRADE_TIER:
        g_value_set_enum (value, self->upgrade_tier);
        break;
    case PROP_ZONE:
        g_value_set_enum (value, self->zone);
        break;
    case PROP_COST_MODIFIER:
        g_value_set_int (value, self->cost_modifier);
        break;
    case PROP_TIMES_PLAYED:
        g_value_set_uint (value, self->times_played);
        break;
    case PROP_BONUS_CHIPS:
        g_value_set_int (value, self->bonus_chips);
        break;
    case PROP_INSTANCE_ID:
        g_value_set_uint64 (value, self->instance_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_instance_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgCardInstance *self = LRG_CARD_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_clear_object (&self->def);
        self->def = g_value_dup_object (value);
        break;
    case PROP_UPGRADE_TIER:
        self->upgrade_tier = g_value_get_enum (value);
        break;
    case PROP_ZONE:
        {
            LrgCardZone old_zone = self->zone;
            self->zone = g_value_get_enum (value);
            if (old_zone != self->zone)
                g_signal_emit (self, signals[SIGNAL_ZONE_CHANGED], 0,
                               old_zone, self->zone);
        }
        break;
    case PROP_COST_MODIFIER:
        self->cost_modifier = g_value_get_int (value);
        break;
    case PROP_TIMES_PLAYED:
        self->times_played = g_value_get_uint (value);
        break;
    case PROP_BONUS_CHIPS:
        self->bonus_chips = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_instance_class_init (LrgCardInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_card_instance_dispose;
    object_class->get_property = lrg_card_instance_get_property;
    object_class->set_property = lrg_card_instance_set_property;

    /**
     * LrgCardInstance:def:
     *
     * The card definition.
     *
     * Since: 1.0
     */
    properties[PROP_DEF] =
        g_param_spec_object ("def",
                             "Definition",
                             "The card definition",
                             LRG_TYPE_CARD_DEF,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:upgrade-tier:
     *
     * The upgrade tier of this card.
     *
     * Since: 1.0
     */
    properties[PROP_UPGRADE_TIER] =
        g_param_spec_enum ("upgrade-tier",
                           "Upgrade Tier",
                           "The upgrade tier of this card",
                           LRG_TYPE_CARD_UPGRADE_TIER,
                           LRG_CARD_UPGRADE_TIER_BASE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:zone:
     *
     * The current zone this card is in.
     *
     * Since: 1.0
     */
    properties[PROP_ZONE] =
        g_param_spec_enum ("zone",
                           "Zone",
                           "The current zone this card is in",
                           LRG_TYPE_CARD_ZONE,
                           LRG_ZONE_DRAW,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:cost-modifier:
     *
     * Temporary cost modifier for this combat.
     *
     * Since: 1.0
     */
    properties[PROP_COST_MODIFIER] =
        g_param_spec_int ("cost-modifier",
                          "Cost Modifier",
                          "Temporary cost modifier",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:times-played:
     *
     * Number of times played this combat.
     *
     * Since: 1.0
     */
    properties[PROP_TIMES_PLAYED] =
        g_param_spec_uint ("times-played",
                           "Times Played",
                           "Number of times played this combat",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:bonus-chips:
     *
     * Bonus chips for scoring.
     *
     * Since: 1.0
     */
    properties[PROP_BONUS_CHIPS] =
        g_param_spec_int ("bonus-chips",
                          "Bonus Chips",
                          "Bonus chips for scoring",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardInstance:instance-id:
     *
     * Unique ID for this card instance.
     *
     * Since: 1.0
     */
    properties[PROP_INSTANCE_ID] =
        g_param_spec_uint64 ("instance-id",
                             "Instance ID",
                             "Unique ID for this card instance",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgCardInstance::zone-changed:
     * @self: the card instance
     * @old_zone: the previous zone
     * @new_zone: the new zone
     *
     * Emitted when the card moves to a different zone.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ZONE_CHANGED] =
        g_signal_new ("zone-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_CARD_ZONE,
                      LRG_TYPE_CARD_ZONE);

    /**
     * LrgCardInstance::upgraded:
     * @self: the card instance
     * @old_tier: the previous tier
     * @new_tier: the new tier
     *
     * Emitted when the card is upgraded.
     *
     * Since: 1.0
     */
    signals[SIGNAL_UPGRADED] =
        g_signal_new ("upgraded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_CARD_UPGRADE_TIER,
                      LRG_TYPE_CARD_UPGRADE_TIER);
}

static void
lrg_card_instance_init (LrgCardInstance *self)
{
    self->upgrade_tier = LRG_CARD_UPGRADE_TIER_BASE;
    self->zone = LRG_ZONE_LIMBO;
    self->instance_id = next_instance_id++;
    self->cost_modifier = 0;
    self->temporary_keywords = LRG_CARD_KEYWORD_NONE;
    self->times_played = 0;
    self->bonus_chips = 0;
}

/* Public API */

LrgCardInstance *
lrg_card_instance_new (LrgCardDef *def)
{
    g_return_val_if_fail (LRG_IS_CARD_DEF (def), NULL);

    return g_object_new (LRG_TYPE_CARD_INSTANCE,
                         "def", def,
                         NULL);
}

LrgCardInstance *
lrg_card_instance_new_upgraded (LrgCardDef         *def,
                                LrgCardUpgradeTier  upgrade_tier)
{
    g_return_val_if_fail (LRG_IS_CARD_DEF (def), NULL);

    return g_object_new (LRG_TYPE_CARD_INSTANCE,
                         "def", def,
                         "upgrade-tier", upgrade_tier,
                         NULL);
}

LrgCardDef *
lrg_card_instance_get_def (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), NULL);

    return self->def;
}

const gchar *
lrg_card_instance_get_id (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), NULL);

    if (self->def == NULL)
        return NULL;

    return lrg_card_def_get_id (self->def);
}

LrgCardUpgradeTier
lrg_card_instance_get_upgrade_tier (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), LRG_CARD_UPGRADE_TIER_BASE);

    return self->upgrade_tier;
}

void
lrg_card_instance_set_upgrade_tier (LrgCardInstance    *self,
                                    LrgCardUpgradeTier  tier)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    if (self->upgrade_tier != tier)
    {
        LrgCardUpgradeTier old_tier = self->upgrade_tier;
        self->upgrade_tier = tier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UPGRADE_TIER]);
        g_signal_emit (self, signals[SIGNAL_UPGRADED], 0, old_tier, tier);
    }
}

gboolean
lrg_card_instance_upgrade (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), FALSE);

    /* Check if card is upgradeable */
    if (self->def == NULL || !lrg_card_def_get_upgradeable (self->def))
    {
        lrg_log_debug ("Card '%s' is not upgradeable",
                       self->def ? lrg_card_def_get_id (self->def) : "(null)");
        return FALSE;
    }

    /* Check if already at max tier */
    if (self->upgrade_tier >= LRG_CARD_UPGRADE_TIER_ULTIMATE)
    {
        lrg_log_debug ("Card '%s' is already at maximum upgrade tier",
                       lrg_card_def_get_id (self->def));
        return FALSE;
    }

    /* Upgrade to next tier */
    lrg_card_instance_set_upgrade_tier (self, self->upgrade_tier + 1);

    lrg_log_debug ("Card '%s' upgraded to tier %d",
                   lrg_card_def_get_id (self->def),
                   self->upgrade_tier);

    return TRUE;
}

LrgCardZone
lrg_card_instance_get_zone (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), LRG_ZONE_DRAW);

    return self->zone;
}

void
lrg_card_instance_set_zone (LrgCardInstance *self,
                            LrgCardZone      zone)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    g_object_set (self, "zone", zone, NULL);
}

gint
lrg_card_instance_get_cost_modifier (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    return self->cost_modifier;
}

void
lrg_card_instance_set_cost_modifier (LrgCardInstance *self,
                                     gint             modifier)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    if (self->cost_modifier != modifier)
    {
        self->cost_modifier = modifier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COST_MODIFIER]);
    }
}

void
lrg_card_instance_add_cost_modifier (LrgCardInstance *self,
                                     gint             modifier)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    lrg_card_instance_set_cost_modifier (self, self->cost_modifier + modifier);
}

LrgCardKeyword
lrg_card_instance_get_temporary_keywords (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), LRG_CARD_KEYWORD_NONE);

    return self->temporary_keywords;
}

void
lrg_card_instance_add_temporary_keyword (LrgCardInstance *self,
                                         LrgCardKeyword   keyword)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    self->temporary_keywords |= keyword;
}

void
lrg_card_instance_remove_temporary_keyword (LrgCardInstance *self,
                                            LrgCardKeyword   keyword)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    self->temporary_keywords &= ~keyword;
}

void
lrg_card_instance_clear_temporary_modifiers (LrgCardInstance *self)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    self->cost_modifier = 0;
    self->temporary_keywords = LRG_CARD_KEYWORD_NONE;
    self->times_played = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COST_MODIFIER]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMES_PLAYED]);
}

gboolean
lrg_card_instance_has_keyword (LrgCardInstance *self,
                               LrgCardKeyword   keyword)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), FALSE);

    /* Check temporary keywords first */
    if ((self->temporary_keywords & keyword) != 0)
        return TRUE;

    /* Check definition keywords */
    if (self->def != NULL)
        return lrg_card_def_has_keyword (self->def, keyword);

    return FALSE;
}

LrgCardKeyword
lrg_card_instance_get_all_keywords (LrgCardInstance *self)
{
    LrgCardKeyword keywords;

    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), LRG_CARD_KEYWORD_NONE);

    keywords = self->temporary_keywords;

    if (self->def != NULL)
        keywords |= lrg_card_def_get_keywords (self->def);

    return keywords;
}

gint
lrg_card_instance_get_effective_cost (LrgCardInstance  *self,
                                      LrgCombatContext *ctx)
{
    gint base_cost;
    gint effective;

    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    if (self->def == NULL)
        return 0;

    /* Get base cost from definition (may be modified by context) */
    base_cost = lrg_card_def_calculate_cost (self->def, ctx);

    /* Apply instance modifier */
    effective = base_cost + self->cost_modifier;

    return MAX (0, effective);
}

guint
lrg_card_instance_get_times_played (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    return self->times_played;
}

void
lrg_card_instance_increment_play_count (LrgCardInstance *self)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    self->times_played++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMES_PLAYED]);
}

void
lrg_card_instance_reset_play_count (LrgCardInstance *self)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    if (self->times_played != 0)
    {
        self->times_played = 0;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMES_PLAYED]);
    }
}

gint
lrg_card_instance_get_bonus_chips (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    return self->bonus_chips;
}

void
lrg_card_instance_set_bonus_chips (LrgCardInstance *self,
                                   gint             chips)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    if (self->bonus_chips != chips)
    {
        self->bonus_chips = chips;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BONUS_CHIPS]);
    }
}

void
lrg_card_instance_add_bonus_chips (LrgCardInstance *self,
                                   gint             chips)
{
    g_return_if_fail (LRG_IS_CARD_INSTANCE (self));

    lrg_card_instance_set_bonus_chips (self, self->bonus_chips + chips);
}

gint
lrg_card_instance_get_total_chip_value (LrgCardInstance *self)
{
    gint base_chips = 0;

    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    if (self->def != NULL)
        base_chips = lrg_card_def_get_chip_value (self->def);

    return base_chips + self->bonus_chips;
}

guint64
lrg_card_instance_get_instance_id (LrgCardInstance *self)
{
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (self), 0);

    return self->instance_id;
}
