/* lrg-enemy-instance.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-enemy-instance.h"
#include "lrg-status-effect-registry.h"
#include "lrg-status-effect-def.h"
#include "../lrg-log.h"

/* Simple status tracking structure (internal use only) */
typedef struct
{
    gchar *status_id;
    gint   stacks;
} StatusEntry;

static StatusEntry *
status_entry_new (const gchar *status_id,
                  gint         stacks)
{
    StatusEntry *entry = g_new0 (StatusEntry, 1);
    entry->status_id = g_strdup (status_id);
    entry->stacks = stacks;
    return entry;
}

static void
status_entry_free (gpointer data)
{
    StatusEntry *entry = data;
    if (entry != NULL)
    {
        g_free (entry->status_id);
        g_free (entry);
    }
}

/**
 * SECTION:lrg-enemy-instance
 * @title: LrgEnemyInstance
 * @short_description: Runtime enemy combatant
 *
 * #LrgEnemyInstance represents an active enemy in combat.
 * It implements #LrgCombatant to participate in the combat
 * system alongside the player.
 *
 * Each instance tracks:
 * - Current and max health
 * - Current block
 * - Active status effects
 * - Current intent
 * - Turn counter for AI patterns
 *
 * Since: 1.0
 */

struct _LrgEnemyInstance
{
    GObject           parent_instance;

    LrgEnemyDef      *def;
    gchar            *instance_id;
    gint              max_health;
    gint              current_health;
    gint              block;
    LrgEnemyIntent   *intent;
    gint              turn_count;

    /* Status effects: id -> StatusEntry* */
    GHashTable       *statuses;

    /* Custom data storage */
    GHashTable       *custom_data;
};

static void lrg_enemy_instance_combatant_init (LrgCombatantInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEnemyInstance, lrg_enemy_instance, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_COMBATANT,
                                                      lrg_enemy_instance_combatant_init))

enum
{
    PROP_0,
    PROP_DEF,
    PROP_MAX_HEALTH,
    PROP_CURRENT_HEALTH,
    PROP_BLOCK,
    PROP_TURN_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_INTENT_CHANGED,
    SIGNAL_DAMAGED,
    SIGNAL_HEALED,
    SIGNAL_BLOCK_CHANGED,
    SIGNAL_STATUS_APPLIED,
    SIGNAL_STATUS_REMOVED,
    SIGNAL_DIED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static gint instance_counter = 0;

/* LrgCombatant interface implementation */

static const gchar *
lrg_enemy_instance_get_id_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return self->instance_id;
}

static const gchar *
lrg_enemy_instance_get_name_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return lrg_enemy_def_get_name (self->def);
}

static gint
lrg_enemy_instance_get_max_health_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return self->max_health;
}

static gint
lrg_enemy_instance_get_current_health_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return self->current_health;
}

static void
lrg_enemy_instance_set_current_health_impl (LrgCombatant *combatant,
                                            gint          health)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);

    health = CLAMP (health, 0, self->max_health);

    if (self->current_health == health)
        return;

    self->current_health = health;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_HEALTH]);

    if (health <= 0)
        g_signal_emit (self, signals[SIGNAL_DIED], 0);
}

static gint
lrg_enemy_instance_get_block_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return self->block;
}

static void
lrg_enemy_instance_set_block_impl (LrgCombatant *combatant,
                                   gint          block)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    gint old_block;

    block = MAX (0, block);

    if (self->block == block)
        return;

    old_block = self->block;
    self->block = block;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLOCK]);
    g_signal_emit (self, signals[SIGNAL_BLOCK_CHANGED], 0, old_block, block);
}

static gint
lrg_enemy_instance_add_block_impl (LrgCombatant *combatant,
                                   gint          amount)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    gint actual_block;
    gint dexterity;

    if (amount <= 0)
        return 0;

    /* Apply dexterity modifier */
    dexterity = lrg_combatant_get_status_stacks (combatant, "dexterity");
    actual_block = amount + dexterity;

    /* Apply frail (25% less block) */
    if (lrg_combatant_has_status (combatant, "frail"))
    {
        actual_block = (gint)(actual_block * 0.75);
    }

    actual_block = MAX (0, actual_block);
    lrg_enemy_instance_set_block_impl (combatant, self->block + actual_block);

    return actual_block;
}

static void
lrg_enemy_instance_clear_block_impl (LrgCombatant *combatant)
{
    lrg_enemy_instance_set_block_impl (combatant, 0);
}

static gint
lrg_enemy_instance_take_damage_impl (LrgCombatant   *combatant,
                                     gint            amount,
                                     LrgEffectFlags  flags)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    gint damage_to_health;
    gint blocked;

    if (amount <= 0)
        return 0;

    /* HP loss bypasses block */
    if (flags & LRG_EFFECT_FLAG_HP_LOSS)
    {
        damage_to_health = amount;
    }
    else
    {
        /* Apply block */
        if (self->block > 0 && !(flags & LRG_EFFECT_FLAG_UNBLOCKABLE))
        {
            blocked = MIN (self->block, amount);
            self->block -= blocked;
            amount -= blocked;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLOCK]);
        }

        damage_to_health = amount;
    }

    if (damage_to_health > 0)
    {
        self->current_health = MAX (0, self->current_health - damage_to_health);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_HEALTH]);
        g_signal_emit (self, signals[SIGNAL_DAMAGED], 0, damage_to_health);

        if (self->current_health <= 0)
            g_signal_emit (self, signals[SIGNAL_DIED], 0);
    }

    return damage_to_health;
}

static gint
lrg_enemy_instance_heal_impl (LrgCombatant *combatant,
                              gint          amount)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    gint actual_heal;

    if (amount <= 0)
        return 0;

    actual_heal = MIN (amount, self->max_health - self->current_health);

    if (actual_heal > 0)
    {
        self->current_health += actual_heal;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_HEALTH]);
        g_signal_emit (self, signals[SIGNAL_HEALED], 0, actual_heal);
    }

    return actual_heal;
}

static gboolean
lrg_enemy_instance_is_alive_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return self->current_health > 0;
}

static gint
lrg_enemy_instance_get_status_stacks_impl (LrgCombatant *combatant,
                                           const gchar  *status_id)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    StatusEntry *entry;

    entry = g_hash_table_lookup (self->statuses, status_id);
    if (entry == NULL)
        return 0;

    return entry->stacks;
}

static gboolean
lrg_enemy_instance_has_status_impl (LrgCombatant *combatant,
                                    const gchar  *status_id)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return g_hash_table_contains (self->statuses, status_id);
}

static gboolean
lrg_enemy_instance_apply_status_impl (LrgCombatant *combatant,
                                      const gchar  *status_id,
                                      gint          stacks)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    StatusEntry *entry;

    if (stacks <= 0)
        return FALSE;

    /* Check artifact (blocks debuffs).
     * Artifact only blocks debuffs -- buffs should pass through.
     */
    if (lrg_combatant_has_status (combatant, "artifact"))
    {
        LrgStatusEffectRegistry *registry;
        LrgStatusEffectDef *def;

        registry = lrg_status_effect_registry_get_default ();
        def = lrg_status_effect_registry_lookup (registry, status_id);

        if (def != NULL && lrg_status_effect_def_is_debuff (def))
        {
            lrg_combatant_remove_status_stacks (combatant, "artifact", 1);
            lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                       "Artifact blocked debuff '%s' on '%s'",
                       status_id, self->instance_id);
            return FALSE;
        }
    }

    entry = g_hash_table_lookup (self->statuses, status_id);

    if (entry != NULL)
    {
        /* Add stacks to existing */
        entry->stacks += stacks;
    }
    else
    {
        /* Create new entry */
        entry = status_entry_new (status_id, stacks);
        g_hash_table_insert (self->statuses, g_strdup (status_id), entry);
    }

    g_signal_emit (self, signals[SIGNAL_STATUS_APPLIED], 0, status_id, stacks);
    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Applied %d stacks of '%s' to '%s'",
               stacks, status_id, self->instance_id);

    return TRUE;
}

static gboolean
lrg_enemy_instance_remove_status_impl (LrgCombatant *combatant,
                                       const gchar  *status_id)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);

    if (!g_hash_table_remove (self->statuses, status_id))
        return FALSE;

    g_signal_emit (self, signals[SIGNAL_STATUS_REMOVED], 0, status_id);
    return TRUE;
}

static void
lrg_enemy_instance_remove_status_stacks_impl (LrgCombatant *combatant,
                                              const gchar  *status_id,
                                              gint          stacks)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    StatusEntry *entry;
    gint remaining;

    if (stacks <= 0)
        return;

    entry = g_hash_table_lookup (self->statuses, status_id);
    if (entry == NULL)
        return;

    remaining = entry->stacks - stacks;

    if (remaining <= 0)
    {
        g_hash_table_remove (self->statuses, status_id);
        g_signal_emit (self, signals[SIGNAL_STATUS_REMOVED], 0, status_id);
    }
    else
    {
        entry->stacks = remaining;
    }
}

static void
lrg_enemy_instance_clear_statuses_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    g_hash_table_remove_all (self->statuses);
}

static GList *
lrg_enemy_instance_get_statuses_impl (LrgCombatant *combatant)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (combatant);
    return g_hash_table_get_values (self->statuses);
}

static void
lrg_enemy_instance_combatant_init (LrgCombatantInterface *iface)
{
    iface->get_id = lrg_enemy_instance_get_id_impl;
    iface->get_name = lrg_enemy_instance_get_name_impl;
    iface->get_max_health = lrg_enemy_instance_get_max_health_impl;
    iface->get_current_health = lrg_enemy_instance_get_current_health_impl;
    iface->set_current_health = lrg_enemy_instance_set_current_health_impl;
    iface->get_block = lrg_enemy_instance_get_block_impl;
    iface->set_block = lrg_enemy_instance_set_block_impl;
    iface->add_block = lrg_enemy_instance_add_block_impl;
    iface->clear_block = lrg_enemy_instance_clear_block_impl;
    iface->take_damage = lrg_enemy_instance_take_damage_impl;
    iface->heal = lrg_enemy_instance_heal_impl;
    iface->is_alive = lrg_enemy_instance_is_alive_impl;
    iface->get_status_stacks = lrg_enemy_instance_get_status_stacks_impl;
    iface->has_status = lrg_enemy_instance_has_status_impl;
    iface->apply_status = lrg_enemy_instance_apply_status_impl;
    iface->remove_status = lrg_enemy_instance_remove_status_impl;
    iface->remove_status_stacks = lrg_enemy_instance_remove_status_stacks_impl;
    iface->clear_statuses = lrg_enemy_instance_clear_statuses_impl;
    iface->get_statuses = lrg_enemy_instance_get_statuses_impl;
}

static void
lrg_enemy_instance_finalize (GObject *object)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (object);

    g_clear_object (&self->def);
    g_free (self->instance_id);
    lrg_enemy_intent_free (self->intent);
    g_hash_table_unref (self->statuses);
    g_hash_table_unref (self->custom_data);

    G_OBJECT_CLASS (lrg_enemy_instance_parent_class)->finalize (object);
}

static void
lrg_enemy_instance_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_value_set_object (value, self->def);
        break;
    case PROP_MAX_HEALTH:
        g_value_set_int (value, self->max_health);
        break;
    case PROP_CURRENT_HEALTH:
        g_value_set_int (value, self->current_health);
        break;
    case PROP_BLOCK:
        g_value_set_int (value, self->block);
        break;
    case PROP_TURN_COUNT:
        g_value_set_int (value, self->turn_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_enemy_instance_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgEnemyInstance *self = LRG_ENEMY_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_clear_object (&self->def);
        self->def = g_value_dup_object (value);
        break;
    case PROP_MAX_HEALTH:
        self->max_health = g_value_get_int (value);
        break;
    case PROP_CURRENT_HEALTH:
        lrg_enemy_instance_set_current_health_impl (LRG_COMBATANT (self),
                                                    g_value_get_int (value));
        break;
    case PROP_BLOCK:
        lrg_enemy_instance_set_block_impl (LRG_COMBATANT (self),
                                           g_value_get_int (value));
        break;
    case PROP_TURN_COUNT:
        self->turn_count = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_enemy_instance_class_init (LrgEnemyInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_enemy_instance_finalize;
    object_class->get_property = lrg_enemy_instance_get_property;
    object_class->set_property = lrg_enemy_instance_set_property;

    properties[PROP_DEF] =
        g_param_spec_object ("def",
                             "Definition",
                             "Enemy definition",
                             LRG_TYPE_ENEMY_DEF,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_int ("max-health",
                          "Max Health",
                          "Maximum health",
                          1, G_MAXINT, 10,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_CURRENT_HEALTH] =
        g_param_spec_int ("current-health",
                          "Current Health",
                          "Current health",
                          0, G_MAXINT, 10,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_BLOCK] =
        g_param_spec_int ("block",
                          "Block",
                          "Current block",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_TURN_COUNT] =
        g_param_spec_int ("turn-count",
                          "Turn Count",
                          "Number of turns taken",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_INTENT_CHANGED] =
        g_signal_new ("intent-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_DAMAGED] =
        g_signal_new ("damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_HEALED] =
        g_signal_new ("healed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_BLOCK_CHANGED] =
        g_signal_new ("block-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[SIGNAL_STATUS_APPLIED] =
        g_signal_new ("status-applied",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

    signals[SIGNAL_STATUS_REMOVED] =
        g_signal_new ("status-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[SIGNAL_DIED] =
        g_signal_new ("died",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_enemy_instance_init (LrgEnemyInstance *self)
{
    self->def = NULL;
    self->instance_id = g_strdup_printf ("enemy-%d", ++instance_counter);
    self->max_health = 10;
    self->current_health = 10;
    self->block = 0;
    self->intent = NULL;
    self->turn_count = 0;
    self->statuses = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, status_entry_free);
    self->custom_data = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
}

/**
 * lrg_enemy_instance_new:
 * @def: the enemy definition
 *
 * Creates a new enemy instance with randomized health.
 *
 * Returns: (transfer full): a new #LrgEnemyInstance
 *
 * Since: 1.0
 */
LrgEnemyInstance *
lrg_enemy_instance_new (LrgEnemyDef *def)
{
    gint base_health;
    gint variance;
    gint actual_health;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (def), NULL);

    base_health = lrg_enemy_def_get_base_health (def);
    variance = lrg_enemy_def_get_health_variance (def);

    if (variance > 0)
        actual_health = base_health + g_random_int_range (-variance, variance + 1);
    else
        actual_health = base_health;

    actual_health = MAX (1, actual_health);

    return lrg_enemy_instance_new_with_health (def, actual_health);
}

/**
 * lrg_enemy_instance_new_with_health:
 * @def: the enemy definition
 * @max_health: specific max health value
 *
 * Creates a new enemy instance with specific health.
 *
 * Returns: (transfer full): a new #LrgEnemyInstance
 *
 * Since: 1.0
 */
LrgEnemyInstance *
lrg_enemy_instance_new_with_health (LrgEnemyDef *def,
                                    gint         max_health)
{
    LrgEnemyInstance *self;

    g_return_val_if_fail (LRG_IS_ENEMY_DEF (def), NULL);
    g_return_val_if_fail (max_health > 0, NULL);

    self = g_object_new (LRG_TYPE_ENEMY_INSTANCE,
                         "def", def,
                         "max-health", max_health,
                         "current-health", max_health,
                         NULL);

    return self;
}

LrgEnemyDef *
lrg_enemy_instance_get_def (LrgEnemyInstance *self)
{
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (self), NULL);
    return self->def;
}

const LrgEnemyIntent *
lrg_enemy_instance_get_intent (LrgEnemyInstance *self)
{
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (self), NULL);
    return self->intent;
}

void
lrg_enemy_instance_set_intent (LrgEnemyInstance *self,
                               LrgEnemyIntent   *intent)
{
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (self));

    lrg_enemy_intent_free (self->intent);
    self->intent = intent;

    g_signal_emit (self, signals[SIGNAL_INTENT_CHANGED], 0);
}

void
lrg_enemy_instance_decide_intent (LrgEnemyInstance *self,
                                  LrgCombatContext *context)
{
    LrgEnemyIntent *new_intent;

    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (self));
    g_return_if_fail (self->def != NULL);

    new_intent = lrg_enemy_def_decide_intent (self->def, self, context);
    lrg_enemy_instance_set_intent (self, new_intent);
}

void
lrg_enemy_instance_execute_intent (LrgEnemyInstance *self,
                                   LrgCombatContext *context)
{
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (self));
    g_return_if_fail (self->def != NULL);

    lrg_enemy_def_execute_intent (self->def, self, context);
}

gint
lrg_enemy_instance_get_turn_count (LrgEnemyInstance *self)
{
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (self), 0);
    return self->turn_count;
}

void
lrg_enemy_instance_increment_turn (LrgEnemyInstance *self)
{
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (self));
    self->turn_count++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TURN_COUNT]);
}

void
lrg_enemy_instance_set_data (LrgEnemyInstance *self,
                             const gchar      *key,
                             gpointer          data,
                             GDestroyNotify    destroy)
{
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (self));
    g_return_if_fail (key != NULL);

    if (destroy != NULL && data != NULL)
    {
        g_hash_table_insert (self->custom_data, g_strdup (key),
                             g_steal_pointer (&data));
        /* Note: we're not storing destroy, so caller must manage lifecycle */
    }
    else
    {
        g_hash_table_insert (self->custom_data, g_strdup (key), data);
    }
}

gpointer
lrg_enemy_instance_get_data (LrgEnemyInstance *self,
                             const gchar      *key)
{
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->custom_data, key);
}
