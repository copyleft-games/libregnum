/* lrg-player-combatant.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-player-combatant.h"
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
 * SECTION:lrg-player-combatant
 * @title: LrgPlayerCombatant
 * @short_description: Player's combat representation
 *
 * #LrgPlayerCombatant represents the player in combat.
 * It implements #LrgCombatant to participate in the combat
 * system alongside enemies.
 *
 * The player combatant tracks:
 * - Health (current and max)
 * - Block
 * - Gold
 * - Status effects
 *
 * Since: 1.0
 */

struct _LrgPlayerCombatant
{
    GObject      parent_instance;

    gchar       *id;
    gchar       *name;
    gint         max_health;
    gint         current_health;
    gint         block;
    gint         gold;

    /* Status effects: id -> StatusEntry* */
    GHashTable  *statuses;
};

static void lrg_player_combatant_combatant_init (LrgCombatantInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgPlayerCombatant, lrg_player_combatant, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_COMBATANT,
                                                      lrg_player_combatant_combatant_init))

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_MAX_HEALTH,
    PROP_CURRENT_HEALTH,
    PROP_BLOCK,
    PROP_GOLD,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_DAMAGED,
    SIGNAL_HEALED,
    SIGNAL_BLOCK_CHANGED,
    SIGNAL_GOLD_CHANGED,
    SIGNAL_STATUS_APPLIED,
    SIGNAL_STATUS_REMOVED,
    SIGNAL_DIED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* LrgCombatant interface implementation */

static const gchar *
lrg_player_combatant_get_id_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->id;
}

static const gchar *
lrg_player_combatant_get_name_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->name;
}

static gint
lrg_player_combatant_get_max_health_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->max_health;
}

static gint
lrg_player_combatant_get_current_health_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->current_health;
}

static void
lrg_player_combatant_set_current_health_impl (LrgCombatant *combatant,
                                              gint          health)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);

    health = CLAMP (health, 0, self->max_health);

    if (self->current_health == health)
        return;

    self->current_health = health;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_HEALTH]);

    if (health <= 0)
        g_signal_emit (self, signals[SIGNAL_DIED], 0);
}

static gint
lrg_player_combatant_get_block_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->block;
}

static void
lrg_player_combatant_set_block_impl (LrgCombatant *combatant,
                                     gint          block)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
lrg_player_combatant_add_block_impl (LrgCombatant *combatant,
                                     gint          amount)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
    lrg_player_combatant_set_block_impl (combatant, self->block + actual_block);

    return actual_block;
}

static void
lrg_player_combatant_clear_block_impl (LrgCombatant *combatant)
{
    lrg_player_combatant_set_block_impl (combatant, 0);
}

static gint
lrg_player_combatant_take_damage_impl (LrgCombatant   *combatant,
                                       gint            amount,
                                       LrgEffectFlags  flags)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
lrg_player_combatant_heal_impl (LrgCombatant *combatant,
                                gint          amount)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
lrg_player_combatant_is_alive_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return self->current_health > 0;
}

static gint
lrg_player_combatant_get_status_stacks_impl (LrgCombatant *combatant,
                                             const gchar  *status_id)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    StatusEntry *entry;

    entry = g_hash_table_lookup (self->statuses, status_id);
    if (entry == NULL)
        return 0;

    return entry->stacks;
}

static gboolean
lrg_player_combatant_has_status_impl (LrgCombatant *combatant,
                                      const gchar  *status_id)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return g_hash_table_contains (self->statuses, status_id);
}

static gboolean
lrg_player_combatant_apply_status_impl (LrgCombatant *combatant,
                                        const gchar  *status_id,
                                        gint          stacks)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
                       "Artifact blocked debuff '%s' on player",
                       status_id);
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
               "Applied %d stacks of '%s' to player",
               stacks, status_id);

    return TRUE;
}

static gboolean
lrg_player_combatant_remove_status_impl (LrgCombatant *combatant,
                                         const gchar  *status_id)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);

    if (!g_hash_table_remove (self->statuses, status_id))
        return FALSE;

    g_signal_emit (self, signals[SIGNAL_STATUS_REMOVED], 0, status_id);
    return TRUE;
}

static void
lrg_player_combatant_remove_status_stacks_impl (LrgCombatant *combatant,
                                                const gchar  *status_id,
                                                gint          stacks)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
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
lrg_player_combatant_clear_statuses_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    g_hash_table_remove_all (self->statuses);
}

static GList *
lrg_player_combatant_get_statuses_impl (LrgCombatant *combatant)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (combatant);
    return g_hash_table_get_values (self->statuses);
}

static void
lrg_player_combatant_combatant_init (LrgCombatantInterface *iface)
{
    iface->get_id = lrg_player_combatant_get_id_impl;
    iface->get_name = lrg_player_combatant_get_name_impl;
    iface->get_max_health = lrg_player_combatant_get_max_health_impl;
    iface->get_current_health = lrg_player_combatant_get_current_health_impl;
    iface->set_current_health = lrg_player_combatant_set_current_health_impl;
    iface->get_block = lrg_player_combatant_get_block_impl;
    iface->set_block = lrg_player_combatant_set_block_impl;
    iface->add_block = lrg_player_combatant_add_block_impl;
    iface->clear_block = lrg_player_combatant_clear_block_impl;
    iface->take_damage = lrg_player_combatant_take_damage_impl;
    iface->heal = lrg_player_combatant_heal_impl;
    iface->is_alive = lrg_player_combatant_is_alive_impl;
    iface->get_status_stacks = lrg_player_combatant_get_status_stacks_impl;
    iface->has_status = lrg_player_combatant_has_status_impl;
    iface->apply_status = lrg_player_combatant_apply_status_impl;
    iface->remove_status = lrg_player_combatant_remove_status_impl;
    iface->remove_status_stacks = lrg_player_combatant_remove_status_stacks_impl;
    iface->clear_statuses = lrg_player_combatant_clear_statuses_impl;
    iface->get_statuses = lrg_player_combatant_get_statuses_impl;
}

static void
lrg_player_combatant_finalize (GObject *object)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (object);

    g_free (self->id);
    g_free (self->name);
    g_hash_table_unref (self->statuses);

    G_OBJECT_CLASS (lrg_player_combatant_parent_class)->finalize (object);
}

static void
lrg_player_combatant_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
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
    case PROP_GOLD:
        g_value_set_int (value, self->gold);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_player_combatant_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgPlayerCombatant *self = LRG_PLAYER_COMBATANT (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_MAX_HEALTH:
        self->max_health = MAX (1, g_value_get_int (value));
        break;
    case PROP_CURRENT_HEALTH:
        lrg_player_combatant_set_current_health_impl (LRG_COMBATANT (self),
                                                      g_value_get_int (value));
        break;
    case PROP_BLOCK:
        lrg_player_combatant_set_block_impl (LRG_COMBATANT (self),
                                             g_value_get_int (value));
        break;
    case PROP_GOLD:
        self->gold = MAX (0, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_player_combatant_class_init (LrgPlayerCombatantClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_player_combatant_finalize;
    object_class->get_property = lrg_player_combatant_get_property;
    object_class->set_property = lrg_player_combatant_set_property;

    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Player identifier",
                             "player",
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Player name",
                             "Player",
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_int ("max-health",
                          "Max Health",
                          "Maximum health",
                          1, G_MAXINT, 80,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_CURRENT_HEALTH] =
        g_param_spec_int ("current-health",
                          "Current Health",
                          "Current health",
                          0, G_MAXINT, 80,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_BLOCK] =
        g_param_spec_int ("block",
                          "Block",
                          "Current block",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_GOLD] =
        g_param_spec_int ("gold",
                          "Gold",
                          "Current gold",
                          0, G_MAXINT, 99,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

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

    signals[SIGNAL_GOLD_CHANGED] =
        g_signal_new ("gold-changed",
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
lrg_player_combatant_init (LrgPlayerCombatant *self)
{
    self->id = g_strdup ("player");
    self->name = g_strdup ("Player");
    self->max_health = 80;
    self->current_health = 80;
    self->block = 0;
    self->gold = 0;
    self->statuses = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, status_entry_free);
}

/**
 * lrg_player_combatant_new:
 * @id: player identifier
 * @name: player display name
 * @max_health: maximum health
 *
 * Creates a new player combatant.
 *
 * Returns: (transfer full): a new #LrgPlayerCombatant
 *
 * Since: 1.0
 */
LrgPlayerCombatant *
lrg_player_combatant_new (const gchar *id,
                          const gchar *name,
                          gint         max_health)
{
    return g_object_new (LRG_TYPE_PLAYER_COMBATANT,
                         "id", id,
                         "name", name,
                         "max-health", max_health,
                         "current-health", max_health,
                         NULL);
}

void
lrg_player_combatant_set_max_health (LrgPlayerCombatant *self,
                                     gint                max_health)
{
    g_return_if_fail (LRG_IS_PLAYER_COMBATANT (self));

    max_health = MAX (1, max_health);

    if (self->max_health == max_health)
        return;

    self->max_health = max_health;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HEALTH]);

    /* Clamp current health if needed */
    if (self->current_health > max_health)
    {
        self->current_health = max_health;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_HEALTH]);
    }
}

gint
lrg_player_combatant_get_gold (LrgPlayerCombatant *self)
{
    g_return_val_if_fail (LRG_IS_PLAYER_COMBATANT (self), 0);
    return self->gold;
}

void
lrg_player_combatant_set_gold (LrgPlayerCombatant *self,
                               gint                gold)
{
    gint old_gold;

    g_return_if_fail (LRG_IS_PLAYER_COMBATANT (self));

    gold = MAX (0, gold);

    if (self->gold == gold)
        return;

    old_gold = self->gold;
    self->gold = gold;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GOLD]);
    g_signal_emit (self, signals[SIGNAL_GOLD_CHANGED], 0, old_gold, gold);
}

gint
lrg_player_combatant_add_gold (LrgPlayerCombatant *self,
                               gint                amount)
{
    g_return_val_if_fail (LRG_IS_PLAYER_COMBATANT (self), 0);

    if (amount > 0)
        lrg_player_combatant_set_gold (self, self->gold + amount);

    return self->gold;
}

gboolean
lrg_player_combatant_remove_gold (LrgPlayerCombatant *self,
                                  gint                amount)
{
    g_return_val_if_fail (LRG_IS_PLAYER_COMBATANT (self), FALSE);

    if (amount <= 0)
        return TRUE;

    if (self->gold < amount)
        return FALSE;

    lrg_player_combatant_set_gold (self, self->gold - amount);
    return TRUE;
}
