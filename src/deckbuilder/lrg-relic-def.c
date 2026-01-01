/* lrg-relic-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicDef - Base class for relic definitions implementation.
 */

#include "lrg-relic-def.h"
#include "../lrg-log.h"

typedef struct
{
    gchar          *id;
    gchar          *name;
    gchar          *description;
    gchar          *flavor_text;
    gchar          *icon;
    LrgRelicRarity  rarity;
    LrgRelicTrigger triggers;
    gint            counter_max;
    gboolean        unique;
    gint            price;
} LrgRelicDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgRelicDef, lrg_relic_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_FLAVOR_TEXT,
    PROP_ICON,
    PROP_RARITY,
    PROP_TRIGGERS,
    PROP_COUNTER_MAX,
    PROP_UNIQUE,
    PROP_PRICE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_relic_def_real_on_obtain (LrgRelicDef *self,
                               gpointer     context)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_remove (LrgRelicDef *self,
                               gpointer     context)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_combat_start (LrgRelicDef *self,
                                     gpointer     context)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_combat_end (LrgRelicDef *self,
                                   gpointer     context,
                                   gboolean     victory)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_turn_start (LrgRelicDef *self,
                                   gpointer     context,
                                   gint         turn)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_turn_end (LrgRelicDef *self,
                                 gpointer     context,
                                 gint         turn)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_card_played (LrgRelicDef *self,
                                    gpointer     context,
                                    gpointer     card)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_card_draw (LrgRelicDef *self,
                                  gpointer     context,
                                  gpointer     card)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_card_exhaust (LrgRelicDef *self,
                                     gpointer     context,
                                     gpointer     card)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_card_discard (LrgRelicDef *self,
                                     gpointer     context,
                                     gpointer     card)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_damage_dealt (LrgRelicDef *self,
                                     gpointer     context,
                                     gpointer     target,
                                     gint         amount)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_damage_received (LrgRelicDef *self,
                                        gpointer     context,
                                        gpointer     source,
                                        gint         amount)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_heal (LrgRelicDef *self,
                             gpointer     context,
                             gint         amount)
{
    /* Default: do nothing */
}

static void
lrg_relic_def_real_on_counter_reached (LrgRelicDef *self,
                                        gpointer     context)
{
    /* Default: do nothing */
}

static gint
lrg_relic_def_real_modify_damage_dealt (LrgRelicDef *self,
                                         gpointer     context,
                                         gint         base_damage,
                                         gpointer     target)
{
    /* Default: no modification */
    return base_damage;
}

static gint
lrg_relic_def_real_modify_damage_received (LrgRelicDef *self,
                                            gpointer     context,
                                            gint         base_damage,
                                            gpointer     source)
{
    /* Default: no modification */
    return base_damage;
}

static gint
lrg_relic_def_real_modify_block_gained (LrgRelicDef *self,
                                         gpointer     context,
                                         gint         base_block)
{
    /* Default: no modification */
    return base_block;
}

static gint
lrg_relic_def_real_modify_heal (LrgRelicDef *self,
                                 gpointer     context,
                                 gint         base_heal)
{
    /* Default: no modification */
    return base_heal;
}

static gint
lrg_relic_def_real_modify_gold_gained (LrgRelicDef *self,
                                        gpointer     context,
                                        gint         base_gold)
{
    /* Default: no modification */
    return base_gold;
}

static gchar *
lrg_relic_def_real_get_tooltip (LrgRelicDef *self,
                                 gpointer     context)
{
    LrgRelicDefPrivate *priv = lrg_relic_def_get_instance_private (self);

    if (priv->description != NULL)
        return g_strdup (priv->description);

    return g_strdup (priv->name);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_relic_def_finalize (GObject *object)
{
    LrgRelicDef *self = LRG_RELIC_DEF (object);
    LrgRelicDefPrivate *priv = lrg_relic_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->flavor_text, g_free);
    g_clear_pointer (&priv->icon, g_free);

    G_OBJECT_CLASS (lrg_relic_def_parent_class)->finalize (object);
}

static void
lrg_relic_def_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgRelicDef *self = LRG_RELIC_DEF (object);
    LrgRelicDefPrivate *priv = lrg_relic_def_get_instance_private (self);

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
    case PROP_FLAVOR_TEXT:
        g_value_set_string (value, priv->flavor_text);
        break;
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_RARITY:
        g_value_set_int (value, priv->rarity);
        break;
    case PROP_TRIGGERS:
        g_value_set_uint (value, priv->triggers);
        break;
    case PROP_COUNTER_MAX:
        g_value_set_int (value, priv->counter_max);
        break;
    case PROP_UNIQUE:
        g_value_set_boolean (value, priv->unique);
        break;
    case PROP_PRICE:
        g_value_set_int (value, priv->price);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_relic_def_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgRelicDef *self = LRG_RELIC_DEF (object);
    LrgRelicDefPrivate *priv = lrg_relic_def_get_instance_private (self);

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
    case PROP_FLAVOR_TEXT:
        g_clear_pointer (&priv->flavor_text, g_free);
        priv->flavor_text = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_clear_pointer (&priv->icon, g_free);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_RARITY:
        priv->rarity = g_value_get_int (value);
        break;
    case PROP_TRIGGERS:
        priv->triggers = g_value_get_uint (value);
        break;
    case PROP_COUNTER_MAX:
        priv->counter_max = g_value_get_int (value);
        break;
    case PROP_UNIQUE:
        priv->unique = g_value_get_boolean (value);
        break;
    case PROP_PRICE:
        priv->price = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_relic_def_class_init (LrgRelicDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_relic_def_finalize;
    object_class->get_property = lrg_relic_def_get_property;
    object_class->set_property = lrg_relic_def_set_property;

    /* Set default virtual method implementations */
    klass->on_obtain = lrg_relic_def_real_on_obtain;
    klass->on_remove = lrg_relic_def_real_on_remove;
    klass->on_combat_start = lrg_relic_def_real_on_combat_start;
    klass->on_combat_end = lrg_relic_def_real_on_combat_end;
    klass->on_turn_start = lrg_relic_def_real_on_turn_start;
    klass->on_turn_end = lrg_relic_def_real_on_turn_end;
    klass->on_card_played = lrg_relic_def_real_on_card_played;
    klass->on_card_draw = lrg_relic_def_real_on_card_draw;
    klass->on_card_exhaust = lrg_relic_def_real_on_card_exhaust;
    klass->on_card_discard = lrg_relic_def_real_on_card_discard;
    klass->on_damage_dealt = lrg_relic_def_real_on_damage_dealt;
    klass->on_damage_received = lrg_relic_def_real_on_damage_received;
    klass->on_heal = lrg_relic_def_real_on_heal;
    klass->on_counter_reached = lrg_relic_def_real_on_counter_reached;
    klass->modify_damage_dealt = lrg_relic_def_real_modify_damage_dealt;
    klass->modify_damage_received = lrg_relic_def_real_modify_damage_received;
    klass->modify_block_gained = lrg_relic_def_real_modify_block_gained;
    klass->modify_heal = lrg_relic_def_real_modify_heal;
    klass->modify_gold_gained = lrg_relic_def_real_modify_gold_gained;
    klass->get_tooltip = lrg_relic_def_real_get_tooltip;

    /**
     * LrgRelicDef:id:
     *
     * The unique identifier for this relic.
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
     * LrgRelicDef:name:
     *
     * The display name for this relic.
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
     * LrgRelicDef:description:
     *
     * The description for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Relic description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:flavor-text:
     *
     * The flavor text for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_FLAVOR_TEXT] =
        g_param_spec_string ("flavor-text",
                             "Flavor Text",
                             "Flavor text",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:icon:
     *
     * The icon path for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:rarity:
     *
     * The rarity tier for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_RARITY] =
        g_param_spec_int ("rarity",
                          "Rarity",
                          "Rarity tier",
                          LRG_RELIC_RARITY_STARTER,
                          LRG_RELIC_RARITY_SPECIAL,
                          LRG_RELIC_RARITY_COMMON,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:triggers:
     *
     * The trigger events for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_TRIGGERS] =
        g_param_spec_uint ("triggers",
                           "Triggers",
                           "Trigger events (LrgRelicTrigger flags)",
                           0, G_MAXUINT,
                           LRG_RELIC_TRIGGER_NONE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:counter-max:
     *
     * The maximum counter value (0 = no counter).
     *
     * Since: 1.0
     */
    properties[PROP_COUNTER_MAX] =
        g_param_spec_int ("counter-max",
                          "Counter Max",
                          "Maximum counter value",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:unique:
     *
     * Whether this relic is unique (only one per run).
     *
     * Since: 1.0
     */
    properties[PROP_UNIQUE] =
        g_param_spec_boolean ("unique",
                              "Unique",
                              "Only one per run",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicDef:price:
     *
     * The base shop price for this relic.
     *
     * Since: 1.0
     */
    properties[PROP_PRICE] =
        g_param_spec_int ("price",
                          "Price",
                          "Base shop price",
                          0, G_MAXINT, 150,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_relic_def_init (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv = lrg_relic_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->flavor_text = NULL;
    priv->icon = NULL;
    priv->rarity = LRG_RELIC_RARITY_COMMON;
    priv->triggers = LRG_RELIC_TRIGGER_NONE;
    priv->counter_max = 0;
    priv->unique = TRUE;
    priv->price = 150;
}

/* ==========================================================================
 * Public API - Constructors
 * ========================================================================== */

/**
 * lrg_relic_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new relic definition.
 *
 * Returns: (transfer full): a new #LrgRelicDef
 *
 * Since: 1.0
 */
LrgRelicDef *
lrg_relic_def_new (const gchar *id,
                    const gchar *name)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_RELIC_DEF,
                         "id", id,
                         "name", name,
                         NULL);
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

const gchar *
lrg_relic_def_get_id (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_relic_def_get_name (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->name;
}

void
lrg_relic_def_set_name (LrgRelicDef *self,
                         const gchar *name)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    g_clear_pointer (&priv->name, g_free);
    priv->name = g_strdup (name);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
lrg_relic_def_get_description (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->description;
}

void
lrg_relic_def_set_description (LrgRelicDef *self,
                                const gchar *description)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    g_clear_pointer (&priv->description, g_free);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

const gchar *
lrg_relic_def_get_flavor_text (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->flavor_text;
}

void
lrg_relic_def_set_flavor_text (LrgRelicDef *self,
                                const gchar *flavor_text)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    g_clear_pointer (&priv->flavor_text, g_free);
    priv->flavor_text = g_strdup (flavor_text);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAVOR_TEXT]);
}

const gchar *
lrg_relic_def_get_icon (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_relic_def_set_icon (LrgRelicDef *self,
                         const gchar *icon)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    g_clear_pointer (&priv->icon, g_free);
    priv->icon = g_strdup (icon);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

LrgRelicRarity
lrg_relic_def_get_rarity (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), LRG_RELIC_RARITY_COMMON);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->rarity;
}

void
lrg_relic_def_set_rarity (LrgRelicDef   *self,
                           LrgRelicRarity rarity)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    priv->rarity = rarity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RARITY]);
}

LrgRelicTrigger
lrg_relic_def_get_triggers (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), LRG_RELIC_TRIGGER_NONE);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->triggers;
}

void
lrg_relic_def_set_triggers (LrgRelicDef    *self,
                             LrgRelicTrigger triggers)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    priv->triggers = triggers;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGERS]);
}

gboolean
lrg_relic_def_has_trigger (LrgRelicDef    *self,
                            LrgRelicTrigger trigger)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), FALSE);

    priv = lrg_relic_def_get_instance_private (self);
    return (priv->triggers & trigger) != 0;
}

gint
lrg_relic_def_get_counter_max (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), 0);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->counter_max;
}

void
lrg_relic_def_set_counter_max (LrgRelicDef *self,
                                gint         counter_max)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));
    g_return_if_fail (counter_max >= 0);

    priv = lrg_relic_def_get_instance_private (self);
    priv->counter_max = counter_max;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNTER_MAX]);
}

gboolean
lrg_relic_def_get_unique (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), TRUE);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->unique;
}

void
lrg_relic_def_set_unique (LrgRelicDef *self,
                           gboolean     unique)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    priv = lrg_relic_def_get_instance_private (self);
    priv->unique = unique;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNIQUE]);
}

gint
lrg_relic_def_get_price (LrgRelicDef *self)
{
    LrgRelicDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), 0);

    priv = lrg_relic_def_get_instance_private (self);
    return priv->price;
}

void
lrg_relic_def_set_price (LrgRelicDef *self,
                          gint         price)
{
    LrgRelicDefPrivate *priv;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));
    g_return_if_fail (price >= 0);

    priv = lrg_relic_def_get_instance_private (self);
    priv->price = price;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRICE]);
}

/* ==========================================================================
 * Public API - Virtual Method Wrappers
 * ========================================================================== */

void
lrg_relic_def_on_obtain (LrgRelicDef *self,
                          gpointer     context)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_obtain != NULL)
        klass->on_obtain (self, context);
}

void
lrg_relic_def_on_remove (LrgRelicDef *self,
                          gpointer     context)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_remove != NULL)
        klass->on_remove (self, context);
}

void
lrg_relic_def_on_combat_start (LrgRelicDef *self,
                                gpointer     context)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_combat_start != NULL)
        klass->on_combat_start (self, context);
}

void
lrg_relic_def_on_combat_end (LrgRelicDef *self,
                              gpointer     context,
                              gboolean     victory)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_combat_end != NULL)
        klass->on_combat_end (self, context, victory);
}

void
lrg_relic_def_on_turn_start (LrgRelicDef *self,
                              gpointer     context,
                              gint         turn)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_turn_start != NULL)
        klass->on_turn_start (self, context, turn);
}

void
lrg_relic_def_on_turn_end (LrgRelicDef *self,
                            gpointer     context,
                            gint         turn)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_turn_end != NULL)
        klass->on_turn_end (self, context, turn);
}

void
lrg_relic_def_on_card_played (LrgRelicDef *self,
                               gpointer     context,
                               gpointer     card)
{
    LrgRelicDefClass *klass;

    g_return_if_fail (LRG_IS_RELIC_DEF (self));

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->on_card_played != NULL)
        klass->on_card_played (self, context, card);
}

gint
lrg_relic_def_modify_damage_dealt (LrgRelicDef *self,
                                    gpointer     context,
                                    gint         base_damage,
                                    gpointer     target)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), base_damage);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->modify_damage_dealt != NULL)
        return klass->modify_damage_dealt (self, context, base_damage, target);

    return base_damage;
}

gint
lrg_relic_def_modify_damage_received (LrgRelicDef *self,
                                       gpointer     context,
                                       gint         base_damage,
                                       gpointer     source)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), base_damage);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->modify_damage_received != NULL)
        return klass->modify_damage_received (self, context, base_damage, source);

    return base_damage;
}

gint
lrg_relic_def_modify_block_gained (LrgRelicDef *self,
                                    gpointer     context,
                                    gint         base_block)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), base_block);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->modify_block_gained != NULL)
        return klass->modify_block_gained (self, context, base_block);

    return base_block;
}

gint
lrg_relic_def_modify_heal (LrgRelicDef *self,
                            gpointer     context,
                            gint         base_heal)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), base_heal);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->modify_heal != NULL)
        return klass->modify_heal (self, context, base_heal);

    return base_heal;
}

gint
lrg_relic_def_modify_gold_gained (LrgRelicDef *self,
                                   gpointer     context,
                                   gint         base_gold)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), base_gold);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->modify_gold_gained != NULL)
        return klass->modify_gold_gained (self, context, base_gold);

    return base_gold;
}

gchar *
lrg_relic_def_get_tooltip (LrgRelicDef *self,
                            gpointer     context)
{
    LrgRelicDefClass *klass;

    g_return_val_if_fail (LRG_IS_RELIC_DEF (self), NULL);

    klass = LRG_RELIC_DEF_GET_CLASS (self);
    if (klass->get_tooltip != NULL)
        return klass->get_tooltip (self, context);

    return g_strdup ("");
}
