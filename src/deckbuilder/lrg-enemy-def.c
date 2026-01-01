/* lrg-enemy-def.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-enemy-def.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-enemy-def
 * @title: LrgEnemyDef
 * @short_description: Enemy type definition
 *
 * #LrgEnemyDef defines an enemy type with its base stats,
 * AI behavior patterns, and lifecycle hooks.
 *
 * The intent system allows enemies to telegraph their actions.
 * By default, intents are selected from a weighted pool of
 * patterns. Subclasses can override decide_intent() for more
 * complex AI behavior.
 *
 * Since: 1.0
 */

typedef struct
{
    LrgEnemyIntent *intent;
    gint            weight;
} IntentPattern;

typedef struct
{
    gchar        *id;
    gchar        *name;
    gchar        *description;
    gchar        *icon;
    LrgEnemyType  enemy_type;
    gint          base_health;
    gint          health_variance;

    /* Intent patterns for default AI */
    GPtrArray    *patterns;
} LrgEnemyDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgEnemyDef, lrg_enemy_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_ENEMY_TYPE,
    PROP_BASE_HEALTH,
    PROP_HEALTH_VARIANCE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
intent_pattern_free (gpointer data)
{
    IntentPattern *pattern = data;
    if (pattern)
    {
        lrg_enemy_intent_free (pattern->intent);
        g_slice_free (IntentPattern, pattern);
    }
}

static void
lrg_enemy_def_finalize (GObject *object)
{
    LrgEnemyDef *self = LRG_ENEMY_DEF (object);
    LrgEnemyDefPrivate *priv = lrg_enemy_def_get_instance_private (self);

    g_free (priv->id);
    g_free (priv->name);
    g_free (priv->description);
    g_free (priv->icon);
    g_ptr_array_unref (priv->patterns);

    G_OBJECT_CLASS (lrg_enemy_def_parent_class)->finalize (object);
}

static void
lrg_enemy_def_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgEnemyDef *self = LRG_ENEMY_DEF (object);
    LrgEnemyDefPrivate *priv = lrg_enemy_def_get_instance_private (self);

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
    case PROP_ENEMY_TYPE:
        g_value_set_enum (value, priv->enemy_type);
        break;
    case PROP_BASE_HEALTH:
        g_value_set_int (value, priv->base_health);
        break;
    case PROP_HEALTH_VARIANCE:
        g_value_set_int (value, priv->health_variance);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_enemy_def_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgEnemyDef *self = LRG_ENEMY_DEF (object);
    LrgEnemyDefPrivate *priv = lrg_enemy_def_get_instance_private (self);

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
    case PROP_ICON:
        g_free (priv->icon);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_ENEMY_TYPE:
        priv->enemy_type = g_value_get_enum (value);
        break;
    case PROP_BASE_HEALTH:
        priv->base_health = g_value_get_int (value);
        break;
    case PROP_HEALTH_VARIANCE:
        priv->health_variance = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* Default AI: weighted random selection from patterns */
static LrgEnemyIntent *
lrg_enemy_def_real_decide_intent (LrgEnemyDef      *self,
                                  LrgEnemyInstance *instance,
                                  LrgCombatContext *context)
{
    LrgEnemyDefPrivate *priv = lrg_enemy_def_get_instance_private (self);
    gint total_weight;
    gint roll;
    guint i;

    if (priv->patterns->len == 0)
    {
        /* No patterns defined, return unknown intent */
        return lrg_enemy_intent_new (LRG_INTENT_UNKNOWN);
    }

    /* Calculate total weight */
    total_weight = 0;
    for (i = 0; i < priv->patterns->len; i++)
    {
        IntentPattern *pattern = g_ptr_array_index (priv->patterns, i);
        total_weight += pattern->weight;
    }

    if (total_weight <= 0)
        return lrg_enemy_intent_new (LRG_INTENT_UNKNOWN);

    /* Roll and select */
    roll = g_random_int_range (0, total_weight);

    for (i = 0; i < priv->patterns->len; i++)
    {
        IntentPattern *pattern = g_ptr_array_index (priv->patterns, i);
        roll -= pattern->weight;
        if (roll < 0)
            return lrg_enemy_intent_copy (pattern->intent);
    }

    /* Fallback */
    return lrg_enemy_intent_new (LRG_INTENT_UNKNOWN);
}

/* Default execute: just log that intent was executed */
static void
lrg_enemy_def_real_execute_intent (LrgEnemyDef      *self,
                                   LrgEnemyInstance *instance,
                                   LrgCombatContext *context)
{
    /* Base implementation does nothing - subclasses or combat manager handle this */
    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Enemy '%s' executing intent (default handler)",
               lrg_enemy_def_get_id (self));
}

static void
lrg_enemy_def_real_on_spawn (LrgEnemyDef      *self,
                             LrgEnemyInstance *instance,
                             LrgCombatContext *context)
{
    /* Default: do nothing */
}

static void
lrg_enemy_def_real_on_death (LrgEnemyDef      *self,
                             LrgEnemyInstance *instance,
                             LrgCombatContext *context)
{
    /* Default: do nothing */
}

static void
lrg_enemy_def_class_init (LrgEnemyDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_enemy_def_finalize;
    object_class->get_property = lrg_enemy_def_get_property;
    object_class->set_property = lrg_enemy_def_set_property;

    klass->decide_intent = lrg_enemy_def_real_decide_intent;
    klass->execute_intent = lrg_enemy_def_real_execute_intent;
    klass->on_spawn = lrg_enemy_def_real_on_spawn;
    klass->on_death = lrg_enemy_def_real_on_death;

    /**
     * LrgEnemyDef:id:
     *
     * Unique identifier for this enemy type.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:name:
     *
     * Display name for this enemy type.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:description:
     *
     * Description of this enemy type.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Enemy description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:icon:
     *
     * Icon path for this enemy type.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon path",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:enemy-type:
     *
     * Type classification (normal, elite, boss, minion).
     *
     * Since: 1.0
     */
    properties[PROP_ENEMY_TYPE] =
        g_param_spec_enum ("enemy-type",
                           "Enemy Type",
                           "Type classification",
                           LRG_TYPE_ENEMY_TYPE,
                           LRG_ENEMY_TYPE_NORMAL,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:base-health:
     *
     * Base health value for this enemy type.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_HEALTH] =
        g_param_spec_int ("base-health",
                          "Base Health",
                          "Base health value",
                          1, G_MAXINT, 10,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgEnemyDef:health-variance:
     *
     * Health variance (±variance from base).
     *
     * Since: 1.0
     */
    properties[PROP_HEALTH_VARIANCE] =
        g_param_spec_int ("health-variance",
                          "Health Variance",
                          "Health variance amount",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_enemy_def_init (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv = lrg_enemy_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->enemy_type = LRG_ENEMY_TYPE_NORMAL;
    priv->base_health = 10;
    priv->health_variance = 0;
    priv->patterns = g_ptr_array_new_with_free_func (intent_pattern_free);
}

/**
 * lrg_enemy_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new enemy definition.
 *
 * Returns: (transfer full): a new #LrgEnemyDef
 *
 * Since: 1.0
 */
LrgEnemyDef *
lrg_enemy_def_new (const gchar *id,
                   const gchar *name)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_ENEMY_DEF,
                         "id", id,
                         "name", name,
                         NULL);
}

const gchar *
lrg_enemy_def_get_id (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), NULL);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_enemy_def_get_name (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), NULL);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->name;
}

void
lrg_enemy_def_set_name (LrgEnemyDef *self,
                        const gchar *name)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) == 0)
        return;

    g_free (priv->name);
    priv->name = g_strdup (name);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
lrg_enemy_def_get_description (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), NULL);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->description;
}

void
lrg_enemy_def_set_description (LrgEnemyDef *self,
                               const gchar *description)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) == 0)
        return;

    g_free (priv->description);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

LrgEnemyType
lrg_enemy_def_get_enemy_type (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), LRG_ENEMY_TYPE_NORMAL);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->enemy_type;
}

void
lrg_enemy_def_set_enemy_type (LrgEnemyDef  *self,
                              LrgEnemyType  type)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);

    if (priv->enemy_type == type)
        return;

    priv->enemy_type = type;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENEMY_TYPE]);
}

gint
lrg_enemy_def_get_base_health (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), 10);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->base_health;
}

void
lrg_enemy_def_set_base_health (LrgEnemyDef *self,
                               gint         health)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);
    health = MAX (1, health);

    if (priv->base_health == health)
        return;

    priv->base_health = health;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_HEALTH]);
}

gint
lrg_enemy_def_get_health_variance (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), 0);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->health_variance;
}

void
lrg_enemy_def_set_health_variance (LrgEnemyDef *self,
                                   gint         variance)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);
    variance = MAX (0, variance);

    if (priv->health_variance == variance)
        return;

    priv->health_variance = variance;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH_VARIANCE]);
}

const gchar *
lrg_enemy_def_get_icon (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), NULL);

    priv = lrg_enemy_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_enemy_def_set_icon (LrgEnemyDef *self,
                        const gchar *icon)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);

    if (g_strcmp0 (priv->icon, icon) == 0)
        return;

    g_free (priv->icon);
    priv->icon = g_strdup (icon);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

LrgEnemyIntent *
lrg_enemy_def_decide_intent (LrgEnemyDef      *self,
                             LrgEnemyInstance *instance,
                             LrgCombatContext *context)
{
    LrgEnemyDefClass *klass;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (self), NULL);

    klass = LRG_ENEMY_DEF_GET_CLASS (self);
    g_return_val_if_fail (klass->decide_intent != NULL, NULL);

    return klass->decide_intent (self, instance, context);
}

void
lrg_enemy_def_execute_intent (LrgEnemyDef      *self,
                              LrgEnemyInstance *instance,
                              LrgCombatContext *context)
{
    LrgEnemyDefClass *klass;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    klass = LRG_ENEMY_DEF_GET_CLASS (self);
    g_return_if_fail (klass->execute_intent != NULL);

    klass->execute_intent (self, instance, context);
}

void
lrg_enemy_def_on_spawn (LrgEnemyDef      *self,
                        LrgEnemyInstance *instance,
                        LrgCombatContext *context)
{
    LrgEnemyDefClass *klass;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    klass = LRG_ENEMY_DEF_GET_CLASS (self);
    if (klass->on_spawn != NULL)
        klass->on_spawn (self, instance, context);
}

void
lrg_enemy_def_on_death (LrgEnemyDef      *self,
                        LrgEnemyInstance *instance,
                        LrgCombatContext *context)
{
    LrgEnemyDefClass *klass;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    klass = LRG_ENEMY_DEF_GET_CLASS (self);
    if (klass->on_death != NULL)
        klass->on_death (self, instance, context);
}

/**
 * lrg_enemy_def_add_intent_pattern:
 * @self: an #LrgEnemyDef
 * @intent: (transfer full): intent to add to pattern
 * @weight: selection weight
 *
 * Adds an intent to the weighted selection pool.
 * Used by the default decide_intent implementation.
 *
 * Since: 1.0
 */
void
lrg_enemy_def_add_intent_pattern (LrgEnemyDef    *self,
                                  LrgEnemyIntent *intent,
                                  gint            weight)
{
    LrgEnemyDefPrivate *priv;
    IntentPattern *pattern;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));
    g_return_if_fail (intent != NULL);
    g_return_if_fail (weight > 0);

    priv = lrg_enemy_def_get_instance_private (self);

    pattern = g_slice_new0 (IntentPattern);
    pattern->intent = intent;
    pattern->weight = weight;

    g_ptr_array_add (priv->patterns, pattern);
}

/**
 * lrg_enemy_def_clear_intent_patterns:
 * @self: an #LrgEnemyDef
 *
 * Clears all intent patterns.
 *
 * Since: 1.0
 */
void
lrg_enemy_def_clear_intent_patterns (LrgEnemyDef *self)
{
    LrgEnemyDefPrivate *priv;

    g_return_if_fail (LRG_IS_ENEMY_DEF (self));

    priv = lrg_enemy_def_get_instance_private (self);
    g_ptr_array_set_size (priv->patterns, 0);
}
